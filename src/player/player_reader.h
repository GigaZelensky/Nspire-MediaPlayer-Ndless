#ifndef NDVIDEO_PLAYER_READER_H
#define NDVIDEO_PLAYER_READER_H
#include "raw_player_io.h"
typedef struct PlayerReader PlayerReader;
PlayerReader *player_reader_create(const char *);
void player_reader_destroy(PlayerReader *);
void player_reader_cancel(PlayerReader *);
int player_reader_read(PlayerReader *, uint64_t, void *, size_t, bool);
void player_reader_service(uint32_t);
bool player_reader_idle(unsigned);
bool player_reader_crypto_step(uint32_t);
bool player_reader_read_step(uint32_t);
bool player_reader_needs_request(const PlayerReader *);
void player_reader_after_clock_reset(PlayerReader *);
int player_reader_error(const PlayerReader *);
uint32_t player_reader_file_bytes(const PlayerReader *);
size_t player_reader_memory_bytes(void);
bool player_reader_last_read(PlayerReader *, uint32_t *, uint32_t *, uint32_t *);
bool player_reader_last_wait(const PlayerReader *, RawPlayerWaitStats *);
void player_reader_debug(FILE *, const PlayerReader *);
#endif
