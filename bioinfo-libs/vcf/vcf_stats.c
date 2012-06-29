#include "vcf_stats.h"


/* ******************************
 *      Whole file statistics   *
 * ******************************/
 
file_stats_t* new_file_stats() {
    return calloc (1, sizeof(file_stats_t));
}

void free_file_stats(file_stats_t* file_stats) {
    free(file_stats);
}

void update_file_stats(int variants_count, int samples_count, int snps_count, int transitions_count, int transversions_count, 
                       int indels_count, int biallelics_count, int multiallelics_count, int pass_count, float accum_quality, 
                       file_stats_t *stats) {
    stats->samples_count = samples_count;
    stats->variants_count += variants_count;
    stats->snps_count += snps_count;
    stats->transitions_count += transitions_count;
    stats->transversions_count += transversions_count;
    stats->indels_count += indels_count;
    stats->biallelics_count += biallelics_count;
    stats->multiallelics_count += multiallelics_count;
    stats->pass_count += pass_count;
    stats->accum_quality += accum_quality;
    stats->mean_quality = accum_quality / variants_count;
}


/* ******************************
 *     Per variant statistics   *
 * ******************************/
 
variant_stats_t* new_variant_stats(char *chromosome, unsigned long position, char *ref_allele) {
    variant_stats_t *stats = (variant_stats_t*) malloc (sizeof(variant_stats_t));
    
    stats->chromosome = chromosome;
    stats->position = position;
    stats->ref_allele = ref_allele;
    stats->alternates = NULL;
    
    stats->num_alleles = 1;
    stats->alleles_count = NULL;
    stats->genotypes_count = NULL;
    stats->alleles_freq = NULL;
    stats->genotypes_freq = NULL;
    
    stats->missing_alleles = 0;
    stats->missing_genotypes = 0;
    
    return stats;
}

void free_variant_stats(variant_stats_t* stats) {
    if (stats->chromosome) { free(stats->chromosome); }
    if (stats->ref_allele) { free(stats->ref_allele); }
    if (stats->alternates) {
//         for (int i = 0; i < stats->num_alleles; i++) {
//             free(stats->alternates[i]);
//         }
        free(stats->alternates);
    }
    if (stats->alleles_count) { free(stats->alleles_count); }
    if (stats->genotypes_count) { free(stats->genotypes_count); }
    if (stats->alleles_freq) { free(stats->alleles_freq); }
    if (stats->genotypes_freq) { free(stats->genotypes_freq); }
    free(stats);
}

int get_variants_stats(vcf_record_t **variants, int num_variants, list_t *output_list, file_stats_t *file_stats) {
    char *copy_buf, *copy_buf2, *token, *sample;
    char *save_strtok;
    
    int num_alternates, gt_pos, cur_pos;
    int allele1, allele2, alleles_code;
    
    // Temporary variables for file stats updating
    int variants_count = 0, samples_count = 0, snps_count = 0, indels_count = 0, pass_count = 0;
    int transitions_count = 0, transversions_count = 0, biallelics_count = 0, multiallelics_count = 0;
    int total_alleles_count = 0, total_genotypes_count = 0;
    float accum_quality = 0;
    
    // Variant stats management
    vcf_record_t *record;
    variant_stats_t *stats;
    for (int i = 0; i < num_variants; i++) {
        record = variants[i];
        stats = new_variant_stats(strdup(record->chromosome), record->position, strdup(record->reference));
        
        // Create list of alternates
        copy_buf = (char*) calloc (strlen(record->alternate)+1, sizeof(char));
        strcat(copy_buf, record->alternate);
        stats->alternates = split(copy_buf, ",", &num_alternates);
        
        if (!strncmp(stats->alternates[0], ".", 1)) {
            stats->num_alleles = 1;
        } else {
            stats->num_alleles = num_alternates + 1;
        }
        LOG_DEBUG_F("num alternates = %d\tnum_alleles = %d\n", num_alternates, stats->num_alleles);
        
        // Create lists of allele and genotypes counters and frequencies
        stats->alleles_count = (int*) calloc (stats->num_alleles, sizeof(int));
        stats->genotypes_count = (int*) calloc (stats->num_alleles * stats->num_alleles, sizeof(int));
        stats->alleles_freq = (float*) calloc (stats->num_alleles, sizeof(float));
        stats->genotypes_freq = (float*) calloc (stats->num_alleles * stats->num_alleles, sizeof(float));
        
        // Get position where GT is in sample
        gt_pos = get_field_position_in_format("GT", strdup(record->format));
        LOG_DEBUG_F("Genotype position = %d\n", gt_pos);
        if (gt_pos < 0) { continue; }   // This variant has no GT field
        
        // Traverse samples and find the present and missing alleles
        for(int j = 0; j < record->samples->size; j++) {
            sample = (char*) array_list_get(j, record->samples);
            
            // Get to GT position
            alleles_code = get_alleles(strdup(sample), gt_pos, &allele1, &allele2);
            LOG_DEBUG_F("sample = %s, alleles = %d/%d\n", sample, allele1, allele2);
            
            if (allele1 < 0 || allele2 < 0) {
                // Missing genotype (one or both alleles missing)
                stats->missing_genotypes++;
                if (allele1 < 0) { 
                    stats->missing_alleles++; 
                } else {
                    stats->alleles_count[allele1]++;
                    total_alleles_count++;
                }
                    
                if (allele2 < 0) { 
                    stats->missing_alleles++;
                } else {
                    stats->alleles_count[allele2]++;
                    total_alleles_count++;
                }
            } else {
                // Both alleles set
                cur_pos = allele1 * (stats->num_alleles) + allele2;
                stats->alleles_count[allele1]++;
                stats->alleles_count[allele2]++;
                stats->genotypes_count[cur_pos]++;
                total_alleles_count += 2;
                total_genotypes_count++;
            }
        }
        
        // Get allele and genotype frequencies
        for (int j = 0; j < stats->num_alleles; j++) {
            stats->alleles_freq[j] = (float) stats->alleles_count[j] / total_alleles_count;
        }
        for (int j = 0; j < stats->num_alleles * stats->num_alleles; j++) {
            stats->genotypes_freq[j] = (float) stats->genotypes_count[j] / total_genotypes_count;
        }
        
        // Update variables finally used to update file_stats_t structure
        variants_count++;
        if (i == 0) { samples_count = record->samples->size; }  // Just once per batch
        if (strcmp(record->id, ".")) { snps_count++; }
        if (!strcmp(record->filter, "PASS")) { pass_count++; }
        if (record->quality >= 0) { accum_quality += record->quality; } // -1 = N/A
        if (stats->num_alleles > 2) {
            multiallelics_count++; 
        } else if (stats->num_alleles > 1) {
            biallelics_count++;
        }
        
        int ref_len = strlen(stats->ref_allele);
        int alt_len;
        for (int j = 0; j < num_alternates; j++) {
            alt_len = strlen(stats->alternates[j]);
            
            if (ref_len != alt_len) {
                indels_count++;
            } else if (ref_len == 1 && alt_len == 1) {
                switch (stats->ref_allele[0]) {
                    case 'C':
                        if (stats->alternates[j][0] == 'T') {
                            transitions_count++;
                        } else {
                            transversions_count++;
                        }
                        break;
                    case 'T':
                        if (stats->alternates[j][0] == 'C') {
                            transitions_count++;
                        } else {
                            transversions_count++;
                        }
                        break;
                    case 'A':
                        if (stats->alternates[j][0] == 'G') {
                            transitions_count++;
                        } else {
                            transversions_count++;
                        }
                        break;
                    case 'G':
                        if (stats->alternates[j][0] == 'A') {
                            transitions_count++;
                        } else {
                            transversions_count++;
                        }
                        break;
                }
            }
        }
        
        // Insert results in output list
        list_item_t *variant_result = list_item_new(i, 0, stats);
        list_insert_item(variant_result, output_list);

    }
    
    // Update file_stats_t structure
#pragma omp critical 
    {
        update_file_stats(variants_count, samples_count, snps_count, transitions_count, transversions_count, 
                          indels_count, biallelics_count, multiallelics_count, pass_count, accum_quality, file_stats);
    }
    
    return 0;
}

