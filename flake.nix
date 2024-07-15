{
  description = "DiskAnn";

  inputs = {
    textapp-pkgs.url = "git+ssh://git@tsa04.isa.ru/textapp/textapp-pkgs";
  };

  outputs = { self, textapp-pkgs }:
    let pkgs = import textapp-pkgs.inputs.nixpkgs {
          system = "x86_64-linux";
          overlays = [ textapp-pkgs.overlays.default
                       self.overlays.default ];
          config = textapp-pkgs.passthru.pkgs-config;
        };
    in {
      overlays.default = final: prev: {
        diskann = final.callPackage ./nix {src=self;};
        diskann_clang = final.callPackage ./nix {
          src=self;
          stdenv = pkgs.llvmPackages_18.stdenv;
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
