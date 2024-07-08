{
  src,
  stdenv,
  cmake,
  libaio,
  boost,
  mkl,
  gperftools,
}:
stdenv.mkDerivation {
  pname = "diskann";
  version = "0.0.1";
  inherit src;

  hardeningDisable = [ "all" ];

  nativeBuildInputs = [cmake];
  buildInputs=[libaio boost mkl gperftools];
  cmakeFlags = [
    "-DOMP_PATH=${mkl}/lib"
    "-DMKL_PATH=${mkl}/lib"
    "-DMKL_INCLUDE_PATH=${mkl}/include"
  ];

  enableParallelBuilding=true;
}
