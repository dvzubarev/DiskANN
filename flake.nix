{
  description = "DiskAnn";

  inputs = {
    nixpkgs.url = "nixpkgs/48bacf585a51d953def8bff32087970f273052e2";
    faiss.url = "github:dvzubarev/faiss";
  };

  outputs = { self, nixpkgs, faiss }:
    let pkgs = import nixpkgs {
          system = "x86_64-linux";
          overlays = [ self.overlays.default faiss.overlays.default ];
          config = {
            allowUnfree = true;
          };
        };
    in {
      overlays.default = final: prev: {
        diskann = final.callPackage ./nix {src=self;};
        diskann_clang = final.callPackage ./nix {
          src=self;
          stdenv = pkgs.llvmPackages_18.stdenv;
          faiss-git=pkgs.faiss-clang-git;
          llvmPackages=pkgs.llvmPackages_18;
        };
        ccls_18 = prev.ccls.override({llvmPackages=final.llvmPackages_18;});
      };

      packages.x86_64-linux = {
        inherit (pkgs)
          diskann;
        default = pkgs.diskann;
      };

      devShells.x86_64-linux = {
        #dev env with clang compiler
        default = pkgs.mkShell.override { stdenv = pkgs.llvmPackages_18.stdenv; } {

          inputsFrom = [ pkgs.diskann_clang ];
          buildInputs = [
            pkgs.ccls_18
            #for llvm-symbolizer
            pkgs.llvmPackages_18.libllvm
            pkgs.gdb
          ];

          shellHook = ''
          '';
        };
      };
    };

}
