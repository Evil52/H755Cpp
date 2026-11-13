void fixtures(char* destination, char* source, void* raw)
{
    // ruleid: h755-no-heap-allocation
    auto* allocated = new int(3);
    // ruleid: h755-no-reinterpret-cast
    auto* converted = reinterpret_cast<int*>(raw);
    // ruleid: h755-no-unchecked-c-buffer-copy
    strcpy(destination, source);
    // ok: h755-no-heap-allocation
    int fixed[2] = {1, 2};
    // ok: h755-no-heap-allocation
    auto value = Value{3};
    // ruleid: h755-no-heap-allocation
    auto* heap_array = new int[2];
    // ruleid: h755-no-heap-allocation
    auto* raw_heap = malloc(8);
}
