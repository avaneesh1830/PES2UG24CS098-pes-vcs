CVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQPCVBNMQP
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0'; // Ensure null-terminated

        ptr = null_byte + 1; // Skip null byte

        // 3. Read the 32-byte binary hash
        if (ptr + HASH_SIZE > end) return -1; 
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

// Helper for qsort to ensure consistent tree hashing
static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

// Serialize a Tree struct into binary format for storage.
// Caller must free(*data_out).
// Returns 0 on success, -1 on error.
int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    // Estimate max size: (6 bytes mode + 1 byte space + 256 bytes name + 1 byte null + 32 bytes hash) per entry
    size_t max_size = tree->count * 296; 
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    // Create a mutable copy to sort entries (Git requirement)
    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];
        
        // Write mode and name (%o writes octal correctly for Git standards)
        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += written + 1; // +1 to step over the null terminator written by sprintf
        
        // Write binary hash
        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

// Build a tree hierarchy from the current index and write all tree
// objects to the object store.
//
// HINTS - Useful functions and concepts for this phase:
//   - index_load      : load the staged files into memory
//   - strchr          : find the first '/' in a path to separate directories from files
//   - strncmp         : compare prefixes to group files belonging to the same subdirectory
//   - Recursion       : you will likely want to create a recursive helper function 
//                       (e.g., `write_tree_level(entries, count, depth)`) to handle nested dirs.
//   - tree_serialize  : convert your populated Tree struct into a binary buffer
//   - object_write    : save that binary buffer to the store as OBJ_TREE
//
// Returns 0 on success, -1 on error.
int tree_from_index(ObjectID *id_out) {
    // TODO: Implement recursive tree building
    // (See Lab Appendix for logical steps)
    (void)id_out;
    return -1;
}
