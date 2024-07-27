#ifndef FAISS_MCQ_H_
#define FAISS_MCQ_H_

#include <string>
#include <cstdint>

#include "distance.h"

namespace diskann {

template <typename T>
void generate_mcq_data(const std::string &data_file_to_use,
                       const std::string &mcq_pivots_path,
                       const std::string &mcq_compressed_vectors_path,
                       const diskann::Metric compareMetric,
                       const double p_val,
                       const std::string& faiss_factory_str);


}


#endif // FAISS_MCQ_H_
