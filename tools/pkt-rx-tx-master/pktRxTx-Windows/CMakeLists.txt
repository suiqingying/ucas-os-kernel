cmake_minimum_required (VERSION 3.0)
project (pktRxTx)

set(PCAP_DIR ${PROJECT_SOURCE_DIR}/WinPcap/4.1beta3-WpdPack)

set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
add_compile_options(-g -O3)

include_directories(${PROJECT_SOURCE_DIR}/../pktRxTx-Linux/Include)
include_directories(${PCAP_DIR}/Include)

aux_source_directory(../pktRxTx-Linux/Source SRCS)

link_directories(${PCAP_DIR}/Lib)
link_libraries(
    libpacket.a
    libwpcap.a
    -lws2_32
)


set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

add_executable(pktRxTx.exe ${SRCS})