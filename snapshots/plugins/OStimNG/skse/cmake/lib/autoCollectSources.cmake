function(add_src_folder path)
    file(GLOB_RECURSE src_files
        "${path}/*.cpp"
    )

    set(sources ${sources} ${src_files} PARENT_SCOPE)
    

    file(GLOB_RECURSE header_files
        "${path}/*.h"
    )

    set(headers ${headers} ${header_files} PARENT_SCOPE)
endfunction()

function(add_include_folder path)
    file(GLOB_RECURSE header_files
        "${path}/*.h"
    )

    set(headers ${headers} ${header_files} PARENT_SCOPE)
endfunction()