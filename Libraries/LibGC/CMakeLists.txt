set(SOURCES
    BlockAllocator.cpp
    Cell.cpp
    CellAllocator.cpp
    ConservativeVector.cpp
    Handle.cpp
    Heap.cpp
    HeapBlock.cpp
    MarkedVector.cpp
    WeakContainer.cpp
)

serenity_lib(LibGC gc)
