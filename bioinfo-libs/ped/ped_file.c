#include "ped_file.h"


//====================================================================================
//  ped_file.c
//  ped file management functions
//====================================================================================

//-----------------------------------------------------
// ped_open
//-----------------------------------------------------


ped_file_t *ped_open(char *filename) {
    size_t len;
    char *data = mmap_file(&len, filename);

    ped_file_t *ped_file = (ped_file_t *) malloc(sizeof(ped_file_t));
    ped_file->filename = filename;
    ped_file->data = data;
    ped_file->data_len = len;
    
    ped_file->families = cp_hashtable_create_by_option(COLLECTION_MODE_DEEP,
                                                       10,
                                                       cp_hash_istring,         // Hash function
                                                       (cp_compare_fn) strcasecmp,     // Key comparison function
                                                       NULL,                    // Key copy function
                                                       NULL,                    // Key destructor function
                                                       NULL,                    // Value copy function
                                                       (cp_destructor_fn) family_free // Value destructor function
                                                      );
    return ped_file;
}


//-----------------------------------------------------
// ped_close and memory freeing
//-----------------------------------------------------

void ped_close(ped_file_t *ped_file, int free_families) {
    // Free string members
    free(ped_file->filename);
   
    // Free records list if asked to
    if (free_families) {
        cp_hashtable_destroy(ped_file->families);
    }
    
    munmap((void*) ped_file->data, ped_file->data_len);
    free(ped_file);
}

void ped_record_free(ped_record_t* ped_record) {
    free(ped_record->family_id);
    free(ped_record->individual_id);
    free(ped_record->father_id);
    free(ped_record->mother_id);
    free(ped_record);
}

//-----------------------------------------------------
// I/O operations (read and write) in various ways
//-----------------------------------------------------

int ped_read(ped_file_t *ped_file) {
    return ped_ragel_read(NULL, 1, ped_file);
}

int ped_read_batches(list_t *batches_list, size_t batch_size, ped_file_t *ped_file) {
    return ped_ragel_read(batches_list, batch_size, ped_file);
}

int ped_write(ped_file_t *ped_file, char *filename) {
    FILE *fd = fopen(filename, "w");
    if (fd < 0) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return 1;
    }
    
    if (ped_write_to_file(ped_file, fd)) {
        fprintf(stderr, "Error writing file: %s\n", filename);
        fclose(fd);
        return 2;
    }
    
    fclose(fd);
    return 0;
}

//-----------------------------------------------------
// load data into the ped_file_t
//-----------------------------------------------------

int add_family(family_t* family, ped_file_t* ped_file) {
    return cp_hashtable_put(ped_file->families, family->id, family) == NULL;
}

int get_num_families(ped_file_t* ped_file) {
    return cp_hashtable_count(ped_file->families);
}

int add_ped_record(ped_record_t* record, ped_file_t *ped_file) {
    // Get family or, should it not exist yet, create it
    family_t *family = cp_hashtable_get(ped_file->families, record->family_id);
    if (family == NULL) {
        family = family_new(record->family_id);
        if (add_family(family, ped_file)) {
            return 1;
        }
    }
    
    // Get parents from family or, should they not exist yet, create them
    individual_t *father = NULL, *mother = NULL;
    if (record->father_id != NULL && family->father != NULL) {
        if (strcasecmp(family->father->id, record->father_id) == 0) {
            father = family->father;
        } else {
            father = individual_new(record->father_id, -9, MALE, NULL, NULL, family);
            family_set_parent(father, family);
        }
    }
    if (record->mother_id != NULL && family->mother != NULL) {
        if (strcasecmp(family->mother->id, record->mother_id) == 0) {
            mother = family->mother;
        } else {
            mother = individual_new(record->mother_id, -9, FEMALE, NULL, NULL, family);
            family_set_parent(mother, family);
        }
    }
    
    // Create individual with the information extracted from the PED record
    individual_t *individual = individual_new(record->individual_id, record->phenotype, record->sex, father, mother, family);
    if (father != NULL && mother != NULL) {
        family_add_child(individual, family);
    } else {
        family_set_parent(individual, family);
    }

    ped_record_free(record);
    
    return 0;
}

