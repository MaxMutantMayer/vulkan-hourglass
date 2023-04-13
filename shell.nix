let
  pkgs = import <nixpkgs> { };

  shell = pkgs.mkShell {
    name = "vulkan_env";
    buildInputs = [
      pkgs.bear
      pkgs.clang
      pkgs.glfw
      pkgs.lldb
      pkgs.vulkan-headers
      pkgs.vulkan-loader
      pkgs.vulkan-validation-layers
      pkgs.xorg.libpthreadstubs
      pkgs.xorg.libX11
      pkgs.xorg.libXdmcp
      pkgs.xorg.libXext
    ];

    shellHook = ''
      CC=clang
      CXX=clang++
    '';
  };
in shell
