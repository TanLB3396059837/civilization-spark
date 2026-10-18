find_path(RDMA_INCLUDE_DIR
        NAMES infiniband/verbs.h rdma/rdma_cma.h
        PATHS /usr/include /usr/local/include)

find_library(IBVERBS_LIBRARY NAMES ibverbs
        PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
find_library(RDMACM_LIBRARY NAMES rdmacm
        PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RDMA
        REQUIRED_VARS RDMA_INCLUDE_DIR IBVERBS_LIBRARY RDMACM_LIBRARY)

if(RDMA_FOUND)
    set(RDMA_LIBRARIES ${IBVERBS_LIBRARY} ${RDMACM_LIBRARY})
    set(RDMA_INCLUDE_DIRS ${RDMA_INCLUDE_DIR})
endif()