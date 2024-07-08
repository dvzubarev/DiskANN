{
  src,
  lib,
  stdenv,
  cmake,
  libaio,
  boost,
  mkl,
  gperftools,
  llvmPackages ? null,
}:
stdenv.mkDerivation {
  pname = "diskann";
  version = "0.0.1";
  inherit src;

  hardeningDisable = [ "all" ];

  nativeBuildInputs = [cmake];
  buildInputs=[libaio boost mkl gperftools]
              ++ lib.optional (llvmPackages != null) llvmPackages.openmp.dev;
  cmakeFlags = [
    "-DOMP_PATH=${mkl}/lib"
    "-DMKL_PATH=${mkl}/lib"
    "-DMKL_INCLUDE_PATH=${mkl}/include"
  ];

  enableParallelBuilding=true;
}
