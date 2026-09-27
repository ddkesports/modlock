# Zig's archiver is a subcommand of the compiler executable.
foreach(language IN ITEMS C CXX)
  set(CMAKE_${language}_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${language}_ARCHIVE_APPEND "<CMAKE_AR> ar q <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${language}_ARCHIVE_FINISH "<CMAKE_RANLIB> ar s <TARGET>")
endforeach()
