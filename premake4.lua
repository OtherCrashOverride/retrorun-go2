#!lua
local output = "./build/" .. _ACTION

solution "retrorun_solution"
   configurations { "Debug", "Release" }


project "retrorun"
   location (output)
   kind "ConsoleApp"
   language "C++"
   includedirs { "src/.", "." }
   files { "src/*.h", "src/*.cpp" }
   buildoptions { "-std=c++11 -Wall" }
   linkoptions { "-Wl,-rpath=. -L../../. -lopenal -levdev -lgo2 -pthread -ldl -lGLESv2 -lrga" }
   
   configuration "Debug"
      flags { "Symbols" }
      defines { "DEBUG" }

   configuration "Release"
      flags { "Optimize" }
      defines { "NDEBUG" }
