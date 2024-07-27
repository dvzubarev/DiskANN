#include "faiss_mcq.h"

#include <faiss/IndexAdditiveQuantizer.h>
#include <faiss/IndexPQ.h>
#include <faiss/index_io.h>
#include <faiss/index_factory.h>


#include "timer.h"
#include "pq_common.h"
#include "utils.h"
#include "partition.h"

#define BLOCK_SIZE 5000000

namespace diskann{

void center_vecs(size_t num_train, uint32_t dim, float* train_data, std::vector<float>& centroid){


    // If we use L2 distance, there is an option to
    // translate all vectors to make them centered and
    // then compute PQ. This needs to be set to false
    // when using PQ for MIPS as such translations dont
    // preserve inner products.
    for (uint64_t d = 0; d < dim; d++)
    {
        for (uint64_t p = 0; p < num_train; p++)
        {
            centroid[d] += train_data[p * dim + d];
        }
        centroid[d] /= num_train;
    }

    for (uint64_t d = 0; d < dim; d++)
    {
        for (uint64_t p = 0; p < num_train; p++)
        {
            train_data[p * dim + d] -= centroid[d];
        }
    }
}




void set_index_opts(faiss::IndexFlatCodes* index){
    index->verbose = true;
    if (faiss::IndexPQ* pq_index = dynamic_cast<faiss::IndexPQ*>(index)){
        pq_index->pq.verbose = true;
        //diskann samples points for training (see MAX_PQ_TRAINING_SET_SIZE == 256k).
        //Set setting so all this points are used and not sampled again inside faiss.

        auto faiss_sampled = pq_index->pq.ksub * pq_index->pq.cp.max_points_per_centroid;
        if (faiss_sampled < MAX_PQ_TRAINING_SET_SIZE){
            pq_index->pq.cp.max_points_per_centroid = MAX_PQ_TRAINING_SET_SIZE / pq_index->pq.ksub + 1;
        }
    }else if (auto* aq_index = dynamic_cast<faiss::IndexAdditiveQuantizer*>(index)){
        aq_index->aq->verbose = true;
    }

}

int train_mcq(float* train_data,
              size_t num_train, uint32_t dim,
              const std::string& faiss_factory_str,
              std::string mcq_path,
              bool make_zero_mean)
{
    std::vector<float> centroid(dim);
    std::fill(std::begin(centroid), std::end(centroid), 0);


    if (make_zero_mean){
        center_vecs(num_train, dim, train_data, centroid);
    }


    //TODO hardcore metric for now
    auto index = faiss::index_factory(dim, faiss_factory_str.c_str(), faiss::MetricType::METRIC_L2);
    std::unique_ptr<faiss::IndexFlatCodes> storage;
    if (faiss::IndexFlatCodes* flat_codes_index = dynamic_cast<faiss::IndexFlatCodes*>(index)){
        storage.reset(flat_codes_index);
    }else{
        delete index;
        throw diskann::ANNException("Faiss index should be subclass of IndexFlatCodes", -1);
    }
    set_index_opts(storage.get());

    Timer timer;
    storage->train(num_train, train_data);
    diskann::cout << timer.elapsed_seconds_for_step("training_faiss_index") << std::endl;

    size_t compressed_vec_bytes = storage->code_size;
    faiss::write_index(storage.get(), mcq_path.c_str());


    //save meta file with all required information
    std::vector<size_t> cumul_bytes(4, 0);
    auto mcq_meta_path = mcq_path + ".meta";
    std::ofstream writer;
    open_file_to_write(writer, mcq_meta_path);
    //mimic diskann format of pq tables
    writer.write((char *)&compressed_vec_bytes, sizeof(uint32_t));
    writer.write((char *)&dim, sizeof(uint32_t));
    diskann::save_bin<float>(mcq_meta_path.c_str(), centroid.data(), 1, (size_t)dim, 8);
    diskann::cout << "Saved mcq data to " << mcq_path << std::endl;

    return 0;

}

int encode_vecs_with_mcq(const std::string &data_file,
                         const std::string &mcq_path,
                         const std::string &mcq_compressed_vectors_path,
                         const std::string& faiss_factory_str)
{

    size_t read_blk_size = 64 * 1024 * 1024;
    cached_ifstream base_reader(data_file, read_blk_size);
    uint32_t npts32;
    uint32_t basedim32;
    base_reader.read((char *)&npts32, sizeof(uint32_t));
    base_reader.read((char *)&basedim32, sizeof(uint32_t));
    size_t num_points = npts32;
    size_t dim = basedim32;


    std::string mcq_meta_path = mcq_path + ".meta";

    if (!file_exists(mcq_path) or !file_exists(mcq_meta_path)){
        std::cout << "ERROR: faiss MCQ file not found" << std::endl;
        throw diskann::ANNException("faiss MCQ file not found", -1);
    }
    size_t temp, mcq_dim, compressed_vec_size;

    get_bin_metadata(mcq_meta_path, compressed_vec_size, mcq_dim);

    if (mcq_dim != dim) {
        diskann::cout << "Error reading mcq meta file " << mcq_path
        << ", file_dim = " << mcq_dim << " but expecting " << dim
        << " dimensions.";
        throw diskann::ANNException("Error reading mcq meta file.", -1, __FUNCSIG__, __FILE__, __LINE__);
    }

    std::unique_ptr<float[]> centroid;
    float* cd;
    diskann::load_bin<float>(mcq_meta_path, cd, temp, mcq_dim, 8);
    centroid.reset(cd);

    if ((mcq_dim != dim) || (temp != 1)){
        diskann::cout << "Error reading mcq meta file " << mcq_path << ". file_dim  = " << mcq_dim
        << ", file_cols = " << temp << " but expecting " << dim << " entries in 1 dimension.";
        throw diskann::ANNException("Error reading mcq meta file.", -1, __FUNCSIG__, __FILE__, __LINE__);
    }

    std::unique_ptr<faiss::Index> index;
    index.reset(faiss::read_index(mcq_path.c_str()));
    index->verbose = true;

    diskann::cout << "Loaded mcq meta information" << std::endl;

    size_t block_size = num_points <= BLOCK_SIZE ? num_points : BLOCK_SIZE;
    if (faiss_factory_str == "qinco"){
        block_size = 50000;
    }

    // std::vector<uint8_t> block_compressed;
    // block_compressed.resize(block_size * compressed_vec_size, 0);

    std::vector<float> block_data_float(block_size * dim);

    size_t num_blocks = DIV_ROUND_UP(num_points, block_size);

    Timer timer;
    for (size_t block = 0; block < num_blocks; block++){
        size_t start_id = block * block_size;
        size_t end_id = (std::min)((block + 1) * block_size, num_points);
        size_t cur_blk_size = end_id - start_id;

        base_reader.read((char *)(block_data_float.data()), sizeof(float) * (cur_blk_size * dim));
        // diskann::convert_types<T, float>(block_data_T.get(), block_data_tmp.get(), cur_blk_size, dim);

        diskann::cout << "Processing points  [" << start_id << ", " << end_id << ").." << std::flush;

        for (size_t p = 0; p < cur_blk_size; p++) {
            for (uint64_t d = 0; d < dim; d++) {
                block_data_float[p * dim + d] -= centroid[d];
            }
        }
        index->add(cur_blk_size, block_data_float.data());

        diskann::cout << ".done." << std::endl;
    }
    diskann::cout << timer.elapsed_seconds_for_step("encoding_vectors") << std::endl;
    faiss::write_index(index.get(), mcq_compressed_vectors_path.c_str());
    return 0;

 
}



template <typename T>
void generate_mcq_data(const std::string &data_file_to_use,
                       const std::string &mcq_path,
                       const std::string &mcq_compressed_vectors_path,
                       const diskann::Metric compareMetric,
                       const double p_val,
                       const std::string& faiss_factory_str)
{

    size_t train_size, train_dim;
    float *train_data;

    if (!file_exists(mcq_path))
    {
        // instantiates train_data with random sample updates train_size
        gen_random_slice<T>(data_file_to_use.c_str(), p_val, train_data, train_size, train_dim);
        diskann::cout << "Training data with " << train_size << " samples loaded." << std::endl;

        bool make_zero_mean = false;
        std::string factory_str = faiss_factory_str;
        if (faiss_factory_str.size() > 1 && faiss_factory_str[0]== 'z' && faiss_factory_str[1] == 'm'){
            make_zero_mean = true;
            factory_str = faiss_factory_str.substr(2);
        }
        if (compareMetric == diskann::Metric::INNER_PRODUCT)
            make_zero_mean = false;


        //TODO pass metric to faiss?
        train_mcq(train_data, train_size, (uint32_t)train_dim,
                  factory_str, mcq_path, make_zero_mean);
        delete[] train_data;
    }
    else
    {
        diskann::cout << "Skip Training with predefined pivots in: " << mcq_path << std::endl;
    }
    encode_vecs_with_mcq(data_file_to_use, mcq_path, mcq_compressed_vectors_path, faiss_factory_str);


}


template void generate_mcq_data<float>(const std::string &data_file_to_use,
                                       const std::string &mcq_pivots_path,
                                       const std::string &mcq_compressed_vectors_path,
                                       const diskann::Metric compareMetric,
                                       const double p_val,
                                       const std::string& faiss_factory_str);

template void generate_mcq_data<uint8_t>(const std::string &data_file_to_use,
                                         const std::string &mcq_pivots_path,
                                         const std::string &mcq_compressed_vectors_path,
                                         const diskann::Metric compareMetric,
                                         const double p_val,
                                         const std::string& faiss_factory_str);
template void generate_mcq_data<int8_t>(const std::string &data_file_to_use,
                                        const std::string &mcq_pivots_path,
                                        const std::string &mcq_compressed_vectors_path,
                                        const diskann::Metric compareMetric,
                                        const double p_val,
                                        const std::string& faiss_factory_str);

}
