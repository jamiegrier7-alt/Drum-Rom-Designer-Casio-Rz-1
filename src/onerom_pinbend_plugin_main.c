// OneROM plugin C entry points and pin matrix definitions used by the host.
#include <stdint.h>
#include <stddef.h>

#include "plugin.h"

#define PIN_COUNT 28u
#define MATRIX_CELL_COUNT ((size_t)PIN_COUNT * (size_t)PIN_COUNT)
#define MATRIX_BYTE_COUNT ((MATRIX_CELL_COUNT + 7u) / 8u)
#define PINBEND_CTX_MAGIC 0x50424D58u  // 'XMBP'
#define PINBEND_CTX_ABI_VERSION 1u
#define RING_ENTRIES_LOG2 5u
#define RING_MASK ((1u << RING_ENTRIES_LOG2) - 1u)

// Minimum firmware set to 0.6.9 to match current plugin examples.
ORA_DEFINE_USER_PLUGIN(
    onerom_pinbend_main,
    0, 1, 0, 0,
    0, 6, 9
);

typedef struct {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t matrix_bytes;
    volatile uint32_t host_commit_seq;
    uint8_t host_matrix[MATRIX_BYTE_COUNT];
    uint32_t applied_commit_seq;
    uint8_t active_matrix[MATRIX_BYTE_COUNT];
    int8_t drives[PIN_COUNT];
    int8_t resolved[PIN_COUNT];
    uint8_t contention_policy;
    ora_log_fn_t log;
    ora_yield_fn_t yield_fn;
    volatile uint32_t * volatile *write_pos_ptr;
    uint32_t read_idx;
    ora_setup_address_monitor_fn_t setup_address_monitor;
    ora_start_address_monitor_fn_t start_address_monitor;
    ora_get_address_monitor_ring_write_pos_fn_t get_ring_write_pos;
    ora_demangle_addr_fn_t demangle_addr;
    ora_get_active_ram_slot_fn_t get_active_ram_slot;
    ora_get_ram_slot_info_fn_t get_ram_slot_info;
    ora_map_addr_to_phys_fn_t map_addr_to_phys;
    ora_map_data_to_phys_fn_t map_data_to_phys;
    ora_demangle_data_fn_t demangle_data;
} pinbend_ctx_t;

static pinbend_ctx_t *g_ctx = NULL;

// Capture ring for address monitor in control mode.
ORA_RING_BUF_DECLARE_32BIT(s_ring_buf, RING_ENTRIES_LOG2);

// Bare-metal plugin builds use -nostdlib; provide memset for compiler-generated calls.
void *memset(void *dst, int value, size_t len) {
    uint8_t *p = (uint8_t *)dst;
    const uint8_t v = (uint8_t)value;
    for (size_t i = 0; i < len; ++i) {
        p[i] = v;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
    return dst;
}

static uint16_t matrix_index(uint8_t y, uint8_t x) {
    return (uint16_t)y * (uint16_t)PIN_COUNT + (uint16_t)x;
}

static uint8_t ring_write_index(const pinbend_ctx_t *ctx) {
    if (ctx == NULL || ctx->write_pos_ptr == NULL || *ctx->write_pos_ptr == NULL) {
        return 0u;
    }
    const volatile uint32_t *base = (volatile uint32_t *)s_ring_buf;
    const volatile uint32_t *w = (volatile uint32_t *)*ctx->write_pos_ptr;
    return (uint8_t)(((uint32_t)(w - base)) & RING_MASK);
}

static uint8_t matrix_get_bit(const uint8_t *matrix, uint8_t y, uint8_t x) {
    if (matrix == NULL) {
        return 0u;
    }
    const uint16_t idx = matrix_index(y, x);
    const uint16_t byte_idx = (uint16_t)(idx >> 3);
    const uint8_t bit_mask = (uint8_t)(1u << (idx & 7u));
    return (matrix[byte_idx] & bit_mask) ? 1u : 0u;
}

static void matrix_set_bit(uint8_t *matrix, uint8_t y, uint8_t x, uint8_t v) {
    if (matrix == NULL) {
        return;
    }
    const uint16_t idx = matrix_index(y, x);
    const uint16_t byte_idx = (uint16_t)(idx >> 3);
    const uint8_t bit_mask = (uint8_t)(1u << (idx & 7u));
    if (v != 0u) {
        matrix[byte_idx] = (uint8_t)(matrix[byte_idx] | bit_mask);
    } else {
        matrix[byte_idx] = (uint8_t)(matrix[byte_idx] & (uint8_t)~bit_mask);
    }
}

static void clear_matrix(uint8_t *matrix) {
    if (matrix == NULL) {
        return;
    }
    for (size_t i = 0; i < MATRIX_BYTE_COUNT; ++i) {
        matrix[i] = 0u;
    }
}

static void copy_matrix(uint8_t *dst, const uint8_t *src) {
    if (dst == NULL || src == NULL) {
        return;
    }
    for (size_t i = 0; i < MATRIX_BYTE_COUNT; ++i) {
        dst[i] = src[i];
    }
}

static void clear_levels(pinbend_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    for (uint8_t i = 0; i < PIN_COUNT; ++i) {
        ctx->drives[i] = -1;
        ctx->resolved[i] = -1;
    }
}

static void init_ctx(pinbend_ctx_t *ctx) {
    clear_matrix(ctx->host_matrix);
    clear_matrix(ctx->active_matrix);
    clear_levels(ctx);
    ctx->magic = PINBEND_CTX_MAGIC;
    ctx->abi_version = PINBEND_CTX_ABI_VERSION;
    ctx->matrix_bytes = MATRIX_BYTE_COUNT;
    ctx->host_commit_seq = 0u;
    ctx->applied_commit_seq = 0u;
    ctx->contention_policy = 2;  // 0 high-dominant, 1 low-dominant, 2 floating.
    ctx->log = NULL;
    ctx->yield_fn = NULL;
    ctx->write_pos_ptr = NULL;
    ctx->read_idx = 0u;
    ctx->setup_address_monitor = NULL;
    ctx->start_address_monitor = NULL;
    ctx->get_ring_write_pos = NULL;
    ctx->demangle_addr = NULL;
    ctx->get_active_ram_slot = NULL;
    ctx->get_ram_slot_info = NULL;
    ctx->map_addr_to_phys = NULL;
    ctx->map_data_to_phys = NULL;
    ctx->demangle_data = NULL;
}

static int set_connection(pinbend_ctx_t *ctx, uint8_t a, uint8_t b, uint8_t connected) {
    if (ctx == NULL || a >= PIN_COUNT || b >= PIN_COUNT || a == b) {
        return 0;
    }
    const uint8_t v = connected ? 1u : 0u;
    matrix_set_bit(ctx->active_matrix, a, b, v);
    matrix_set_bit(ctx->active_matrix, b, a, v);
    matrix_set_bit(ctx->host_matrix, a, b, v);
    matrix_set_bit(ctx->host_matrix, b, a, v);
    ctx->host_commit_seq += 1u;
    ctx->applied_commit_seq = ctx->host_commit_seq;
    return 1;
}

static int mix_levels(uint8_t any_high, uint8_t any_low, uint8_t policy) {
    if (any_high && any_low) {
        if (policy == 0u) {
            return 1;
        }
        if (policy == 1u) {
            return 0;
        }
        return -1;
    }
    if (any_high) {
        return 1;
    }
    if (any_low) {
        return 0;
    }
    return -1;
}

static uint8_t addr_bit(uint32_t addr, uint8_t bit) {
    return (uint8_t)((addr >> bit) & 1u);
}

static void build_pin_levels(pinbend_ctx_t *ctx, uint32_t logical_addr, uint8_t logical_data, int8_t levels[PIN_COUNT]) {
    for (uint8_t i = 0; i < PIN_COUNT; ++i) {
        levels[i] = -1;
    }

    // Address pins by package position (0-based index):
    levels[0] = (int8_t)addr_bit(logical_addr, 14);   // pin 1 A14
    levels[1] = (int8_t)addr_bit(logical_addr, 12);   // pin 2 A12
    levels[2] = (int8_t)addr_bit(logical_addr, 7);    // pin 3 A7
    levels[3] = (int8_t)addr_bit(logical_addr, 6);    // pin 4 A6
    levels[4] = (int8_t)addr_bit(logical_addr, 5);    // pin 5 A5
    levels[5] = (int8_t)addr_bit(logical_addr, 4);    // pin 6 A4
    levels[6] = (int8_t)addr_bit(logical_addr, 3);    // pin 7 A3
    levels[7] = (int8_t)addr_bit(logical_addr, 2);    // pin 8 A2
    levels[8] = (int8_t)addr_bit(logical_addr, 1);    // pin 9 A1
    levels[9] = (int8_t)addr_bit(logical_addr, 0);    // pin 10 A0
    levels[20] = (int8_t)addr_bit(logical_addr, 10);  // pin 21 A10
    levels[22] = (int8_t)addr_bit(logical_addr, 11);  // pin 23 A11
    levels[23] = (int8_t)addr_bit(logical_addr, 9);   // pin 24 A9
    levels[24] = (int8_t)addr_bit(logical_addr, 8);   // pin 25 A8
    levels[25] = (int8_t)addr_bit(logical_addr, 13);  // pin 26 A13
    levels[26] = (int8_t)addr_bit(logical_addr, 15);  // pin 27 A15

    // Data pins by package position.
    levels[10] = (int8_t)((logical_data >> 0) & 1u);  // pin 11 D0
    levels[11] = (int8_t)((logical_data >> 1) & 1u);  // pin 12 D1
    levels[12] = (int8_t)((logical_data >> 2) & 1u);  // pin 13 D2
    levels[14] = (int8_t)((logical_data >> 3) & 1u);  // pin 15 D3
    levels[15] = (int8_t)((logical_data >> 4) & 1u);  // pin 16 D4
    levels[16] = (int8_t)((logical_data >> 5) & 1u);  // pin 17 D5
    levels[17] = (int8_t)((logical_data >> 6) & 1u);  // pin 18 D6
    levels[18] = (int8_t)((logical_data >> 7) & 1u);  // pin 19 D7

    // Fixed/assumed levels for power/control during active read.
    levels[13] = 0;  // pin 14 GND
    levels[19] = 0;  // pin 20 /CE active low during selected read
    levels[21] = 0;  // pin 22 /OE active low during output-enable
    levels[27] = 1;  // pin 28 VCC

    // Start each read with plugin-configured drive hints for data pins.
    for (uint8_t i = 0; i < PIN_COUNT; ++i) {
        if (ctx->drives[i] >= 0) {
            levels[i] = ctx->drives[i];
        }
    }
}

static uint8_t bend_data_byte(pinbend_ctx_t *ctx, uint32_t logical_addr, uint8_t logical_data) {
    int8_t levels[PIN_COUNT];
    int8_t resolved[PIN_COUNT];
    uint8_t parent[PIN_COUNT];
    uint8_t rank[PIN_COUNT];

    build_pin_levels(ctx, logical_addr, logical_data, levels);

    for (uint8_t i = 0; i < PIN_COUNT; ++i) {
        parent[i] = i;
        rank[i] = 0;
    }

    for (uint8_t i = 0; i < PIN_COUNT; ++i) {
        for (uint8_t j = (uint8_t)(i + 1u); j < PIN_COUNT; ++j) {
            if (matrix_get_bit(ctx->active_matrix, i, j) == 0u) {
                continue;
            }

            uint8_t a = i;
            while (parent[a] != a) {
                a = parent[a];
            }
            uint8_t b = j;
            while (parent[b] != b) {
                b = parent[b];
            }
            if (a == b) {
                continue;
            }
            if (rank[a] < rank[b]) {
                parent[a] = b;
            } else if (rank[b] < rank[a]) {
                parent[b] = a;
            } else {
                parent[b] = a;
                rank[a] += 1u;
            }
        }
    }

    for (uint8_t i = 0; i < PIN_COUNT; ++i) {
        uint8_t root = i;
        while (parent[root] != root) {
            root = parent[root];
        }
        parent[i] = root;
    }

    for (uint8_t i = 0; i < PIN_COUNT; ++i) {
        uint8_t any_high = 0u;
        uint8_t any_low = 0u;
        for (uint8_t j = 0; j < PIN_COUNT; ++j) {
            if (parent[j] != parent[i]) {
                continue;
            }
            if (levels[j] > 0) {
                any_high = 1u;
            } else if (levels[j] == 0) {
                any_low = 1u;
            }
        }
        resolved[i] = (int8_t)mix_levels(any_high, any_low, ctx->contention_policy);
        ctx->resolved[i] = resolved[i];
    }

    uint8_t out = logical_data;
    const uint8_t data_pins[8] = {10u, 11u, 12u, 14u, 15u, 16u, 17u, 18u};
    for (uint8_t bit = 0; bit < 8u; ++bit) {
        const int8_t lv = resolved[data_pins[bit]];
        if (lv > 0) {
            out = (uint8_t)(out | (uint8_t)(1u << bit));
        } else if (lv == 0) {
            out = (uint8_t)(out & (uint8_t)~(uint8_t)(1u << bit));
        }
    }
    return out;
}

static void resolve_levels(pinbend_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    uint8_t visited[PIN_COUNT];
    uint8_t stack[PIN_COUNT];
    uint8_t component[PIN_COUNT];

    for (uint8_t i = 0; i < PIN_COUNT; ++i) {
        visited[i] = 0;
    }

    for (uint8_t start = 0; start < PIN_COUNT; ++start) {
        if (visited[start]) {
            continue;
        }

        uint8_t stack_len = 0;
        uint8_t comp_len = 0;
        stack[stack_len++] = start;
        visited[start] = 1;

        while (stack_len > 0) {
            const uint8_t p = stack[--stack_len];
            component[comp_len++] = p;

            for (uint8_t q = 0; q < PIN_COUNT; ++q) {
                if (visited[q]) {
                    continue;
                }
                if (matrix_get_bit(ctx->active_matrix, p, q) != 0u) {
                    visited[q] = 1;
                    stack[stack_len++] = q;
                }
            }
        }

        uint8_t any_high = 0;
        uint8_t any_low = 0;
        for (uint8_t i = 0; i < comp_len; ++i) {
            const int8_t d = ctx->drives[component[i]];
            if (d > 0) {
                any_high = 1;
            } else if (d == 0) {
                any_low = 1;
            }
        }

        const int8_t mixed = (int8_t)mix_levels(any_high, any_low, ctx->contention_policy);
        for (uint8_t i = 0; i < comp_len; ++i) {
            ctx->resolved[component[i]] = mixed;
        }
    }
}

// Optional helpers exported for host-side tests or wrappers.
int onerom_pinbend_set_connection(uint8_t pin_a, uint8_t pin_b, int connected) {
    return set_connection(g_ctx, pin_a, pin_b, (connected != 0) ? 1u : 0u);
}

int onerom_pinbend_set_pin_level(uint8_t pin, int level) {
    if (g_ctx == NULL || pin >= PIN_COUNT) {
        return 0;
    }
    if (level < 0) {
        g_ctx->drives[pin] = -1;
    } else if (level == 0) {
        g_ctx->drives[pin] = 0;
    } else {
        g_ctx->drives[pin] = 1;
    }
    return 1;
}

void onerom_pinbend_set_policy(int policy) {
    if (g_ctx == NULL) {
        return;
    }
    if (policy <= 0) {
        g_ctx->contention_policy = 0;
    } else if (policy == 1) {
        g_ctx->contention_policy = 1;
    } else {
        g_ctx->contention_policy = 2;
    }
}

void onerom_pinbend_resolve(void) {
    resolve_levels(g_ctx);
}

int onerom_pinbend_get_pin_level(uint8_t pin) {
    if (g_ctx == NULL || pin >= PIN_COUNT) {
        return -1;
    }
    return (int)g_ctx->resolved[pin];
}

void onerom_pinbend_main(
    ora_lookup_fn_t ora_lookup_fn,
    ora_plugin_type_t plugin_type,
    const ora_entry_args_t *entry_args
) {
    (void)entry_args;

    ora_alloc_fn_t alloc = ora_lookup_fn(ORA_ID_ALLOC);
    ora_log_fn_t log = ora_lookup_fn(ORA_ID_LOG);
    ora_set_plugin_context_fn_t set_plugin_context = ora_lookup_fn(ORA_ID_SET_PLUGIN_CONTEXT);
    ora_yield_fn_t yield_fn = ora_lookup_fn(ORA_ID_YIELD);

    pinbend_ctx_t *ctx = (alloc != NULL) ? alloc(sizeof(pinbend_ctx_t)) : NULL;
    if (ctx == NULL) {
        if (log != NULL) {
            log("pinbend: alloc ctx failed");
        }
        return;
    }

    ctx->log = log;
    ctx->yield_fn = yield_fn;
    init_ctx(ctx);
    ctx->setup_address_monitor = ora_lookup_fn(ORA_ID_SETUP_ADDRESS_MONITOR);
    ctx->start_address_monitor = ora_lookup_fn(ORA_ID_START_ADDRESS_MONITOR);
    ctx->get_ring_write_pos = ora_lookup_fn(ORA_ID_GET_ADDRESS_MONITOR_RING_WRITE_POS);
    ctx->demangle_addr = ora_lookup_fn(ORA_ID_DEMANGLE_ADDR);
    ctx->get_active_ram_slot = ora_lookup_fn(ORA_ID_GET_ACTIVE_RAM_SLOT);
    ctx->get_ram_slot_info = ora_lookup_fn(ORA_ID_GET_RAM_SLOT_INFO);
    ctx->map_addr_to_phys = ora_lookup_fn(ORA_ID_MAP_ADDR_TO_PHYS);
    ctx->map_data_to_phys = ora_lookup_fn(ORA_ID_MAP_DATA_TO_PHYS);
    ctx->demangle_data = ora_lookup_fn(ORA_ID_DEMANGLE_DATA);
    g_ctx = ctx;

    if (set_plugin_context != NULL) {
        set_plugin_context(plugin_type, ctx);
    }

    if (log != NULL) {
        log("One ROM pinbend plugin started (28x28 matrix)");
    }

    if (ctx->setup_address_monitor == NULL || ctx->start_address_monitor == NULL ||
        ctx->get_ring_write_pos == NULL || ctx->demangle_addr == NULL ||
        ctx->get_active_ram_slot == NULL || ctx->get_ram_slot_info == NULL ||
        ctx->map_addr_to_phys == NULL || ctx->map_data_to_phys == NULL || ctx->demangle_data == NULL) {
        if (log != NULL) {
            log("pinbend: missing required API functions");
        }
        return;
    }

    if (ctx->setup_address_monitor((volatile uint32_t *)s_ring_buf, RING_ENTRIES_LOG2, ORA_MONITOR_MODE_CONTROL, 32u, NULL) != ORA_RESULT_OK) {
        if (log != NULL) {
            log("pinbend: setup_address_monitor failed");
        }
        return;
    }
    ctx->write_pos_ptr = ctx->get_ring_write_pos();
    ctx->start_address_monitor();

    // Keep plugin alive; future work can monitor address bus and alter served data.
    while (1) {
        if (ctx->host_commit_seq != ctx->applied_commit_seq) {
            copy_matrix(ctx->active_matrix, ctx->host_matrix);
            ctx->applied_commit_seq = ctx->host_commit_seq;
        }

        uint8_t active_slot = 0u;
        uint32_t slot_base = 0u;
        uint32_t slot_size = 0u;
        const ora_result_t slot_rc = ctx->get_active_ram_slot(&active_slot);
        if (slot_rc == ORA_RESULT_OK) {
            (void)ctx->get_ram_slot_info(active_slot, &slot_base, &slot_size, NULL);
        }

        if (slot_base != 0u && slot_size != 0u) {
            uint8_t write_idx = ring_write_index(ctx);
            while (ctx->read_idx != write_idx) {
                const uint32_t phys_capture = ((volatile uint32_t *)s_ring_buf)[ctx->read_idx];
                ctx->read_idx = (ctx->read_idx + 1u) & RING_MASK;

                uint32_t logical_addr = 0u;
                if (ctx->demangle_addr(phys_capture, &logical_addr, 1u) != ORA_RESULT_OK) {
                    continue;
                }
                if (logical_addr >= slot_size) {
                    continue;
                }

                const uint32_t phys_off = ctx->map_addr_to_phys(logical_addr);
                if (phys_off >= slot_size) {
                    continue;
                }

                const volatile uint8_t *slot_mem = (volatile uint8_t *)(uintptr_t)slot_base;
                const uint8_t raw = slot_mem[phys_off];
                uint8_t logical_data = 0u;
                if (ctx->demangle_data(raw, &logical_data) != ORA_RESULT_OK) {
                    continue;
                }

                const uint8_t bent_data = bend_data_byte(ctx, logical_addr, logical_data);
                if (bent_data == logical_data) {
                    continue;
                }

                const uint8_t phys_data = ctx->map_data_to_phys(bent_data);
                if (phys_data != raw) {
                    ((volatile uint8_t *)(uintptr_t)slot_base)[phys_off] = phys_data;
                }

                write_idx = ring_write_index(ctx);
            }
        }

        if (yield_fn != NULL) {
            (void)yield_fn(NULL);
        }
    }
}
