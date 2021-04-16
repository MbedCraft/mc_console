#if !defined __MC_CONSOLE_H__
# define __MC_CONSOLE_H__

void mc_console_init(const char * const history_path);
void mc_console_run(const char * const prompt_str, size_t prompt_str_size);

#endif // __MC_CONSOLE_H__
