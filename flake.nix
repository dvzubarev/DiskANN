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
      };

      packages.x86_64-linux = {
        inherit (pkgs)
          diskann;
        default = pkgs.diskann;
      };

      devShells.x86_64-linux = {
        default = pkgs.mkShell {

          inputsFrom = [ pkgs.diskann ];
          buildInputs = [
            pkgs.ccls
            pkgs.gdb
          ];

          shellHook = '''';
        };
        exp = pkgs.mkShell {
          buildInputs = [
            pkgs.diskann
          ];
          shellHook = '''';
        };
      };
    };

}
