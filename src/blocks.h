/**
 * @file blocks.h
 * @brief Block parsing functions
 * 
 * This file provides all the functions needed to parse all the
 * blocks present in a `.itl` file.
 */

struct msdh* itlp_parse_msdh(char **msdh_string, const char *buffer_end);
struct mith* itlp_parse_mith(char **mith_string, const char *buffer_end);
struct mhoh* itlp_parse_mhoh(char **mhoh_string, const char *buffer_end);
struct mfdh* itlp_parse_mfdh(char **pos_buffer, const char *buffer_end);
struct miph* itlp_parse_miph(char **pos_buffer, const char *buffer_end);
struct mlth* itlp_parse_mlth(char **mlth_string, const char *buffer_end);
struct mhgh* itlp_parse_mhgh(char **mhgh_string, const char *buffer_end);
struct mlah* itlp_parse_mlah(char **mlah_string, const char *buffer_end);
struct miah* itlp_parse_miah(char **miah_string, const char *buffer_end);
struct mlih* itlp_parse_mlih(char **mlih_string, const char *buffer_end);
struct mlrh* itlp_parse_mlrh(char **mlrh_string, const char *buffer_end);
struct mlsh* itlp_parse_mlsh(char **mlsh_string, const char *buffer_end);
struct stsh* itlp_parse_stsh(char **stsh_string, const char *buffer_end);
struct msph* itlp_parse_msph(char **msph_string, const char *buffer_end);
struct mlph* itlp_parse_mlph(char **mlph_string, const char *buffer_end);
struct mtph* itlp_parse_mtph(char **mtph_string, const char *buffer_end);
struct miih* itlp_parse_miih(char **miih_string, const char *buffer_end);
struct mprh* itlp_parse_mprh(char **mprh_string, const char *buffer_end);
struct mlqh* itlp_parse_mlqh(char **mlqh_string, const char *buffer_end);
struct miqh* itlp_parse_miqh(char **miqh_string, const char *buffer_end);
struct file* itlp_parse_file(char **file_string, const char *buffer_end);
