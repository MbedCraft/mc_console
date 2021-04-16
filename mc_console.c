#include <stdio.h>

#include "driver/uart.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"

#include "mc_console.h"

static const char * _history_path;

void mc_console_init(const char * const history_path) {
    _history_path = history_path;

    /* Drain stdout before reconfiguring it */
    fflush(stdout);
    fsync(fileno(stdout));

    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

    /* Configure UART. Note that REF_TICK is used so that the baud rate remains
     * correct while APB frequency is changing in light sleep mode.
     */
    const uart_config_t uart_config = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
        .source_clk = UART_SCLK_REF_TICK,
#else
        .source_clk = UART_SCLK_XTAL,
#endif
    };
    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
            256, 0, 0, NULL, 0) );
    ESP_ERROR_CHECK( uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config) );

    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    /* Initialize the console */
    esp_console_config_t console_config = {
            .max_cmdline_args = 8,
            .max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
            .hint_color = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config) );

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(100);

    /* Don't return empty lines */
    linenoiseAllowEmpty(false);

    if (NULL != _history_path) {
        /* Load command history from filesystem */
        linenoiseHistoryLoad(_history_path);
    }

    esp_console_register_help_command();
}

void mc_console_run(const char * const prompt_str, size_t prompt_str_size) {
    char *prompt = NULL;
    int prompt_size;

    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    prompt_size = snprintf(
            prompt, 0, "%s%.*s%s",
            LOG_COLOR_I, prompt_str_size, prompt_str, "> " LOG_RESET_COLOR
            );
    prompt_size++; // increase by one for the '\0' character
    prompt = malloc(prompt_size); // allocate prompt string

    assert(prompt != NULL); // Sanity check on prompt allocation

    snprintf(
            prompt, prompt_size, "%s%.*s%s",
            LOG_COLOR_I, prompt_str_size, prompt_str, "> " LOG_RESET_COLOR
            );

    printf("\n"
            "Type 'help' to get the list of commands.\n"
            "Use UP/DOWN arrows to navigate through command history.\n"
            "Press TAB when typing command name to auto-complete.\n"
            "Press Enter or Ctrl+C will terminate the console environment.\n");

    /* Figure out if the terminal supports escape sequences */
    int probe_status = linenoiseProbe();
    if (probe_status) { /* zero indicates success */
        printf("\n"
                "Your terminal application does not support escape sequences.\n"
                "Line editing and history features are disabled.\n"
                "On Windows, try using Putty instead.\n");
        linenoiseSetDumbMode(1);
        /* Since the terminal doesn't support escape sequences,
         * don't use color codes in the prompt.
         */
        free(prompt);
        prompt_size = snprintf(prompt, 0, "%s%s", prompt_str, "> ");
        prompt_size++; // increase prompt_size by one for the '\0' character
        prompt = malloc(prompt_size); // allocate prompt string
        snprintf(prompt, prompt_size, "%s%s", prompt_str, "> ");
    }

    /* Main loop */
    while(true) {
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         */
        char* line = linenoise(prompt);
        if (line == NULL) { /* Break on EOF or error */
            break;
        }
        /* Add the command to the history if not empty*/
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
            /* Save command history to filesystem */
            linenoiseHistorySave(_history_path);
        }

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }

    ESP_LOGE(__func__, "Error or end-of-input, terminating console");
    esp_console_deinit();
}
