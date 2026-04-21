
        size_t name_len = null_byte - ptr;
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

// Forward declarations (implemented in object.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int hex_to_hash(const char *hex, ObjectID *id_out);

typedef struct {
    char name[256];
} SubdirName;

static int subdir_exists(SubdirName *subs, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(subs[i].name, name) == 0) return 1;
    }
    return 0;
}

typedef struct {
    uint32_t mode;
    ObjectID hash;
    char path[512];
} TreeIndexEntry;

typedef struct {
    TreeIndexEntry entries[MAX_TREE_ENTRIES];
    int count;
} TreeIndex;

static int tree_index_load(TreeIndex *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0; // Empty index if file is absent.

    while (index->count < MAX_TREE_ENTRIES) {
        TreeIndexEntry e;
        char hash_hex[HASH_HEX_SIZE + 1];
        uint64_t mtime_unused;
        unsigned int size_unused;
        int scanned = fscanf(
            f,
            "%o %64s %" SCNu64 " %u %511[^\n]\n",
            &e.mode, hash_hex, &mtime_unused, &size_unused, e.path
        );
        if (scanned == EOF) break;
        if (scanned != 5 || hex_to_hash(hash_hex, &e.hash) != 0) {
            fclose(f);
            return -1;
        }
        index->entries[index->count++] = e;
    }

    fclose(f);
    return 0;
}

static int write_tree_level(const TreeIndex *index, const char *prefix, ObjectID *out_id) {
    Tree tree;
    tree.count = 0;

    SubdirName subdirs[MAX_TREE_ENTRIES];
    int subdir_count = 0;

    size_t prefix_len = strlen(prefix);
    for (int i = 0; i < index->count; i++) {
        const char *path = index->entries[i].path;
        if (strncmp(path, prefix, prefix_len) != 0) continue;

        const char *rest = path + prefix_len;
        if (*rest == '\0') continue;

        const char *slash = strchr(rest, '/');
        if (!slash) {
            if (tree.count >= MAX_TREE_ENTRIES) return -1;
            TreeEntry *entry = &tree.entries[tree.count++];
            entry->mode = index->entries[i].mode;
            entry->hash = index->entries[i].hash;
            size_t rest_len = strlen(rest);
            if (rest_len >= sizeof(entry->name)) return -1;
            memcpy(entry->name, rest, rest_len + 1);
            continue;
        }

        size_t name_len = (size_t)(slash - rest);
        if (name_len == 0 || name_len >= sizeof(subdirs[0].name)) return -1;

        char dirname[256];
        memcpy(dirname, rest, name_len);
        dirname[name_len] = '\0';

        if (!subdir_exists(subdirs, subdir_count, dirname)) {
            if (subdir_count >= MAX_TREE_ENTRIES) return -1;
            memcpy(subdirs[subdir_count].name, dirname, name_len + 1);
            subdir_count++;
        }
    }

    for (int i = 0; i < subdir_count; i++) {
        char child_prefix[1024];
        int plen = snprintf(child_prefix, sizeof(child_prefix), "%s%s/", prefix, subdirs[i].name);
        if (plen < 0 || (size_t)plen >= sizeof(child_prefix)) return -1;

        ObjectID child_id;
        if (write_tree_level(index, child_prefix, &child_id) != 0) return -1;

        if (tree.count >= MAX_TREE_ENTRIES) return -1;
        TreeEntry *entry = &tree.entries[tree.count++];
        entry->mode = MODE_DIR;
        entry->hash = child_id;
        size_t dir_len = strlen(subdirs[i].name);
        if (dir_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, subdirs[i].name, dir_len + 1);
    }

    void *serialized = NULL;
    size_t serialized_len = 0;
    if (tree.count == 0) {
        serialized = malloc(1);
        if (!serialized) return -1;
        serialized_len = 0;
    } else {
        if (tree_serialize(&tree, &serialized, &serialized_len) != 0) return -1;
    }

    int rc = object_write(OBJ_TREE, serialized, serialized_len, out_id);
    free(serialized);
    return rc;
}

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
    TreeIndex index;
    if (tree_index_load(&index) != 0) return -1;
    return write_tree_level(&index, "", id_out);
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
