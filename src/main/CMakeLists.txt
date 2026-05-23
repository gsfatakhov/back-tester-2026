# executable
set(TARGET back-tester)
add_executable(${TARGET} main.cpp)
target_link_libraries(${TARGET} PRIVATE
    back-tester-order-book
)
