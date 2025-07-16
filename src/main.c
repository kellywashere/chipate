#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <raylib.h>

#include "display.h"
#include "audio.h"

typedef unsigned char byte;
typedef unsigned short word;

const int instructions_per_frame = 128;
//const int max_instructions_to_run = 40;
const int max_instructions_to_run = -1;

// QUIRKS: see https://github.com/Timendus/chip8-test-suite?tab=readme-ov-file#quirks-test
const bool enable_vf_quirk = true;
const bool enable_memory_quirk = true; // see: https://tobiasvl.github.io/blog/write-a-chip-8-emulator/#fx55-and-fx65-store-and-load-memory
const bool enable_display_wait = true;
const bool enable_clipping = true; // when false, sprites wrap around
const bool enable_shift_quirk = false; // if false, copies vy to vx before shifting
const bool enable_jump_quirk = false; // When true, instruction (Bnnn) doesn't use v0, but vX instead where X is the highest nibble of nnn

#define VF 15

const int PIXELW = 12;
const int PIXELH = 12;

bool DEBUG = false;

byte font[] = {
	0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
	0x20, 0x60, 0x20, 0x20, 0x70, // 1
	0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
	0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
	0x90, 0x90, 0xF0, 0x10, 0x10, // 4
	0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
	0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
	0xF0, 0x10, 0x20, 0x40, 0x40, // 7
	0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
	0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
	0xF0, 0x90, 0xF0, 0x90, 0x90, // A
	0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
	0xF0, 0x80, 0x80, 0x80, 0xF0, // C
	0xE0, 0x90, 0x90, 0x90, 0xE0, // D
	0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
	0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

int keys[] = {
/*
1	2	3	C
4	5	6	D
7	8	9	E
A	0	B	F
*/
	KEY_X, KEY_ONE, KEY_TWO, KEY_THREE, KEY_Q, KEY_W, KEY_E, KEY_A,
	KEY_S, KEY_D, KEY_Z, KEY_C, KEY_FOUR, KEY_R, KEY_F, KEY_V
};

struct chip8 {
	word  PC;         // program counter
	word  I;          // address register
	byte  V[16];      // gen purpose registers
	byte  ST;         // sound timer
	byte  DT;         // delay timer
	byte  SP;         // stack pointer;

	word  stack[16];
	byte  mem[4096];
	
	// display
	struct display* display;

	// keypress helper (instr Fx0A)
	bool key_waiting;
	bool keys_pressed[sizeof(keys)/sizeof(keys[0])];
	int  key_regidx;
};


byte chip8_memget(struct chip8* chip8, int addr);


// **************************************** 
//    DISPLAY
// **************************************** 

void chip8_copy_fontdata(struct chip8* chip8) {
	for (int ii = 0; ii < 16 * 5; ++ii)
		chip8->mem[ii] = font[ii];
}

void chip8_cls(struct chip8* chip8) {
	display_clear(chip8->display);
}

void chip8_draw(struct chip8* chip8, int x, int y, int n) {
	int w = chip8->display->width;
	int h = chip8->display->height;

	if (DEBUG) printf("draw(V%X, V%X, %d)\n", x, y, n);
	int addr = chip8->I;
	bool vf = false;
	int yy = chip8->V[y] % h;
	while (n && yy < h) {
		byte b = chip8_memget(chip8, addr);
		int xx = chip8->V[x] % w;
		for (int ii = 0; ii < 8 && xx < w; ++ii) {
			bool bit = ((b >> (7 - ii)) & 1);
			bool pixel = display_get_pixel(chip8->display, xx, yy);
			vf = vf || (bit && pixel); // set VF when XOR leads to pixel reset
			display_set_pixel(chip8->display, xx, yy, bit != pixel); // XOR
			xx = enable_clipping ? xx + 1 : (xx + 1) % w;
		}
		++addr;
		yy = enable_clipping ? yy + 1 : (yy + 1) % h;
		--n;
	}
	chip8->V[VF] = vf ? 1 : 0;
}


// **************************************** 
//    CHIP8
// **************************************** 

#define DISPLAY_WIDTH  64
#define DISPLAY_HEIGHT 32


struct chip8* chip8_create() {
	struct chip8* chip8 = calloc(1,sizeof(struct chip8));
	chip8->display = display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
	chip8_copy_fontdata(chip8);
	return chip8;
}

void chip8_destroy(struct chip8* chip8) {
	if (chip8) {
		display_destroy(chip8->display);
		free(chip8);
	}
}

void chip8_memset(struct chip8* chip8, int addr, byte val) {
	if (addr >= 4096) {
		TraceLog(LOG_ERROR, "Memset: Invalid address %X @ %04X", addr, chip8->PC - 2);
	}
	chip8->mem[addr] = val;
}

byte chip8_memget(struct chip8* chip8, int addr) {
	if (addr >= 4096) {
		TraceLog(LOG_ERROR, "Memget: Invalid address %X @ %04X", addr, chip8->PC - 2);
		return 0;
	}
	return chip8->mem[addr];
}

bool chip8_is_keypressed(struct chip8* chip8, int x) {
	return IsKeyDown(keys[x & 0x0F]);
}

bool chip8_is_anykeypressed(struct chip8* chip8, byte* key) {
	for (int ii = 0; ii < 0x0F; ++ii) {
		if (chip8_is_keypressed(chip8, ii)) {
			*key = ii;
			return true;
		}
	}
	return false;
}

void chip8_dectimers(struct chip8* chip8) {
	chip8->DT = chip8->DT > 0 ? chip8->DT - 1 : 0;
	chip8->ST = chip8->ST > 0 ? chip8->ST - 1 : 0;
	audio_set_beep(chip8->ST > 0);
}

void chip8_ret(struct chip8* chip8) {
	if (chip8->SP == 0) {
		TraceLog(LOG_ERROR, "RET: SP == 0 @ %04X\n", chip8->PC - 2);
		return;
	}
	chip8->PC = chip8->stack[--chip8->SP];
}

void chip8_call(struct chip8* chip8, int addr) {
	if (chip8->SP == 15) {
		TraceLog(LOG_ERROR, "CALL: SP == 15 @ %04X\n", chip8->PC - 2);
		return;
	}
	chip8->stack[chip8->SP++] = chip8->PC;
	chip8->PC = addr;
}

void chip8_opcode8(struct chip8* chip8, int n, int x, int y) {
	int tmp;
	switch (n) {
		case 0: // LD Vx, Vy
			chip8->V[x] = chip8->V[y];
			break;
		case 1: // OR Vx, Vy
			chip8->V[x] = chip8->V[x] | chip8->V[y];
			chip8->V[VF] = enable_vf_quirk ? 0 : chip8->V[VF];
			break;
		case 2: // AND Vx, Vy
			chip8->V[x] = chip8->V[x] & chip8->V[y];
			chip8->V[VF] = enable_vf_quirk ? 0 : chip8->V[VF];
			break;
		case 3: // XOR Vx, Vy
			chip8->V[x] = chip8->V[x] ^ chip8->V[y];
			chip8->V[VF] = enable_vf_quirk ? 0 : chip8->V[VF];
			break;
		case 4: // ADD Vx, Vy
			tmp = chip8->V[x] + chip8->V[y];
			chip8->V[x] = tmp & 0x0FF;
			chip8->V[VF] = tmp >= 256 ? 1 : 0;
			break;
		case 5: // SUB Vx, Vy
			tmp = chip8->V[x] - chip8->V[y];
			chip8->V[x] = tmp & 0x0FF;
			chip8->V[VF] = tmp < 0 ? 0 : 1;
			break;
		case 6: // SHR Vx
			if (!enable_shift_quirk)
				chip8->V[x] = chip8->V[y];
			tmp = chip8->V[x] & 1;
			chip8->V[x] >>= 1;
			chip8->V[VF] = tmp;
			break;
		case 7: // SUBN Vx, Vy
			tmp = chip8->V[y] - chip8->V[x];
			chip8->V[x] = tmp & 0x0FF;
			chip8->V[VF] = tmp < 0 ? 0 : 1;
			break;
		case 0xE: // SHL Vx
			if (!enable_shift_quirk)
				chip8->V[x] = chip8->V[y];
			tmp = (chip8->V[x] >> 7) & 1;
			chip8->V[x] = (chip8->V[x] << 1) & 0x0FF;
			chip8->V[VF] = tmp;
			break;
	}
}

void chip8_opcodeE(struct chip8* chip8, int nn, int x) {
	bool keypressed = chip8_is_keypressed(chip8, chip8->V[x]);
	switch (nn) {
		case 0x9E: // SKP Vx
			chip8->PC = keypressed ? chip8->PC + 2 : chip8->PC;
			break;
		case 0xA1: // SKP Vx
			chip8->PC = !keypressed ? chip8->PC + 2 : chip8->PC;
			break;
	}
}

void chip8_opcodeF(struct chip8* chip8, int nn, int x) {
	int tmp;
	switch (nn) {
		case 0x07: // LD Vx, DT
			chip8->V[x] = chip8->DT;
			break;
		case 0x0A: // LD Vx, key
			chip8->key_waiting = true;
			chip8->key_regidx = x;
			for (int ii = 0; ii < sizeof(keys)/sizeof(keys[0]); ++ii)
				chip8->keys_pressed[ii] = false;
			break;
		case 0x15: // LD DT, Vx
			chip8->DT = chip8->V[x];
			break;
		case 0x18: // LD ST, Vx
			chip8->ST = chip8->V[x];
			break;
		case 0x1E: // ADD I, Vx
			chip8->I = (chip8->I + chip8->V[x]) & 0x0FFF;
			break;
		case 0x29: // LD I, Vx
			chip8->I = 5 * (chip8->V[x] & 0x0F);
			break;
		case 0x33: // LD BCD, Vx
			tmp = chip8->V[x];
			for (int offset = 2; offset >= 0; --offset) {
				int d = tmp % 10;
				tmp /= 10;
				chip8_memset(chip8, chip8->I + offset, d);
			}
			break;
		case 0x55: // LD [I], Vx
			for (int ii = 0; ii <= x; ++ii)
				chip8_memset(chip8, chip8->I + ii, chip8->V[ii]);
			if (enable_memory_quirk)
				chip8->I += x + 1;
			break;
		case 0x65: // LD [I], Vx
			for (int ii = 0; ii <= x; ++ii)
				chip8->V[ii] = chip8_memget(chip8, chip8->I + ii);
			if (enable_memory_quirk)
				chip8->I += x + 1;
			break;
	}
}

void chip8_instr(struct chip8* chip8) {
	if (chip8->key_waiting)
		return;
	chip8->key_waiting = false; // set to true on instr Fx0A

	int instr = (chip8_memget(chip8, chip8->PC) << 8) | chip8_memget(chip8, chip8->PC + 1);
	if (DEBUG) printf("%04X: %04X\n", chip8->PC, instr);
	chip8->PC += 2;
	// split in parts for convenience
	int opc = (instr >> 12) & 0x0F;
	int nnn = instr & 0x0FFF;
	int n = instr & 0x0F;
	int x = (instr >> 8) & 0x0F;
	int y = (instr >> 4) & 0x0F;
	int kk = instr & 0x0FF;
	switch (opc) {
		case 0:
			if (instr == 0x00E0) // CLS
				chip8_cls(chip8);
			else if (instr == 0x00EE) // RET
				chip8_ret(chip8);
			break;
		case 1: // JP addr
			chip8->PC = instr & 0x0FFF;
			break;
		case 2: // CALL addr
			chip8_call(chip8, instr & 0x0FFF);
			break;
		case 3: // SE Vx, byte
			chip8->PC = chip8->V[x] == kk ? chip8->PC + 2 : chip8->PC;
			break;
		case 4: // SNE Vx, byte
			chip8->PC = chip8->V[x] != kk ? chip8->PC + 2 : chip8->PC;
			break;
		case 5: // SE Vx, Vy
			chip8->PC = chip8->V[x] == chip8->V[y] ? chip8->PC + 2 : chip8->PC;
			break;
		case 6: // LD Vx, byte
			if (DEBUG) printf("\tV%d = %d\n", x, kk);
			chip8->V[x] = kk;
			break;
		case 7: // ADD Vx, byte
			chip8->V[x] = (chip8->V[x] + kk) & 0x0FF;
			break;
		case 8:
			chip8_opcode8(chip8, n, x, y); // several sub instructions
			break;
		case 9: // SNE Vx, Vy
			chip8->PC = chip8->V[x] != chip8->V[y] ? chip8->PC + 2 : chip8->PC;
			break;
		case 0xA: // LD I, addr
			if (DEBUG) printf("\tI = %03X\n", nnn);
			chip8->I = nnn;
			break;
		case 0xB: // JP V0, addr
			chip8->PC = (nnn + chip8->V[enable_jump_quirk ? x : 0]) & 0x0FFF;
			break;
		case 0xC: // RND Vx, byte
			chip8->V[x] = GetRandomValue(0, 255) & kk;
			break;
		case 0xD:
			chip8_draw(chip8, x, y, n);
			break;
		case 0xE:
			chip8_opcodeE(chip8, nnn & 0x0FF, x);
			break;
		case 0xF:
			chip8_opcodeF(chip8, nnn & 0x0FF, x);
			break;
	}
}

void chip8_check_press_and_release(struct chip8* chip8) {
	for (int ii = 0; ii < sizeof(keys)/sizeof(keys[0]); ++ii) {
		// mark keys pressed this frame
		if (IsKeyPressed(keys[ii]))
			chip8->keys_pressed[ii] = true;
		// check for released keys that were marked earlier
		if (IsKeyReleased(keys[ii]) && chip8->keys_pressed[ii]) {
			chip8->key_waiting = false;
			chip8->V[chip8->key_regidx] = ii;
		}
	}
}

bool chip8_next_instr_is_draw(struct chip8* chip8) { // for display quirk
	int instrmsb = chip8_memget(chip8, chip8->PC);
	return (instrmsb >> 4) == 0x0D;
}

bool chip8_load_romfile(struct chip8* chip8, const char* romfile, int* romsize) {
	FILE *f = fopen(romfile, "rb");
	if (!f) {
		TraceLog(LOG_ERROR, "Error loading ROM file %s", romfile);
		return false;
	}
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	*romsize = fsize > 4096 - 0x200 ? 4096 - 0x200 : fsize;
	fread(chip8->mem + 0x200, *romsize, 1, f);
	fclose(f);
	chip8->PC = 0x200;
	return true;
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
		printf("Usage: %s romfile\n", argv[0]);
		return 1;
	}
	SetTraceLogLevel(LOG_WARNING);

	struct chip8* chip8 = chip8_create();
	int romsize = 0;
	if (!chip8_load_romfile(chip8, argv[1], &romsize))
		return 2;

	printf("Read %s: %d bytes\n", argv[1], romsize);

	InitWindow(PIXELW * chip8->display->width, PIXELH * chip8->display->height, "ChipAte");
	SetTargetFPS(60);

	audio_init();

	while (!WindowShouldClose()) {

		if (chip8->key_waiting) {
			chip8_check_press_and_release(chip8);
		}
		else {
			for (int ii = 0; !chip8->key_waiting && ii < instructions_per_frame; ++ii) {
				if (enable_display_wait && ii > 0 && chip8_next_instr_is_draw(chip8))
					break;
				chip8_instr(chip8);
			}
		}

		BeginDrawing();
			ClearBackground(BLACK);
			display_show(chip8->display, 0, 0, PIXELW, PIXELH);
		EndDrawing();

		chip8_dectimers(chip8);
		// TODO: make sound if ST > 0
	}

	audio_exit();
	chip8_destroy(chip8);

	CloseWindow();
	return 0;
}

