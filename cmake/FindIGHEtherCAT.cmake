find_path(IGHEtherCAT_INCLUDE_DIR
    NAMES ecrt.h
    PATHS /usr/local/include /usr/include
)

find_library(IGHEtherCAT_LIBRARY
    NAMES ethercat
    PATHS /usr/local/lib /usr/lib /usr/lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(IGHEtherCAT
    REQUIRED_VARS IGHEtherCAT_INCLUDE_DIR IGHEtherCAT_LIBRARY
)

if(IGHEtherCAT_FOUND AND NOT TARGET IGHEtherCAT::IGHEtherCAT)
    add_library(IGHEtherCAT::IGHEtherCAT UNKNOWN IMPORTED)
    set_target_properties(IGHEtherCAT::IGHEtherCAT PROPERTIES
        IMPORTED_LOCATION "${IGHEtherCAT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${IGHEtherCAT_INCLUDE_DIR}"
    )
endif()
