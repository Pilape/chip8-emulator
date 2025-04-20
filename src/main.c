#include <SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 32
#define SCALE 16

#define COLOR_ON 0x77FF33
#define COLOR_OFF 0x223500

struct {
    SDL_Texture* texture;
    SDL_Window* window;
    SDL_Renderer* renderer;
    
    uint32_t SDL_display[SCREEN_WIDTH * SCREEN_HEIGHT];

} SDL_state;

#include "window.c"

typedef struct {
    uint16_t opcode;

    uint8_t memory[4090]; // 4kB ram
    uint8_t display[SCREEN_WIDTH][SCREEN_HEIGHT]; // 1-bit screen
   
    // Registers
    uint8_t V[16]; // General purpose registers
    uint16_t I; // Index register
    uint16_t pc; // Program counter
    uint8_t VF;

    uint16_t stack[16]; // We love the stack
    uint8_t sp; // Stack pointer

    char* keys; // Input (would be an array of 16 but a pointer works better with SDL)

    uint8_t delayTimer;
    uint8_t soundTimer;

} chip8;

void UpdateWindowDisplay(chip8* cpu)
{
    // Blit to SDL display
    for (int x=0; x<SCREEN_WIDTH; x++)
    {
        for (int y=0; y<SCREEN_HEIGHT; y++)
        {
            uint32_t color = (cpu->display[x][y]) ? COLOR_ON : COLOR_OFF;
            SDL_state.SDL_display[y*SCREEN_WIDTH+x] = color;
        }
    }
    // Present display
    SDL_UpdateTexture(SDL_state.texture, NULL, SDL_state.SDL_display, SCREEN_WIDTH * 4);
    SDL_RenderCopy(SDL_state.renderer, SDL_state.texture, NULL, NULL);
    SDL_RenderPresent(SDL_state.renderer);
}

uint8_t fontSet[80] = {
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

chip8 InitProgram(char* path)
{
    chip8 cpu = { 0 };

    srand(time(NULL)); // Initialize rng

    // Load font
    for (int i=0; i<80; i++)
    {
        cpu.memory[i] = fontSet[i];
    }

    cpu.pc = 0x200; // Program starts at 200

    // Load ROM
    FILE* romFile = fopen(path, "rb"); // Read in binary mode
   
    // Get ROM size
    fseek(romFile, 0, SEEK_END);
    uint64_t fileSize = ftell(romFile);
    rewind(romFile);

    printf("Read %lu bytes from %s\n", fileSize, path);

    uint8_t* buffer = malloc(fileSize+1 * sizeof(uint8_t));
    fread(buffer, fileSize, 1, romFile);

    fclose(romFile);

    for (int i=0; i<fileSize; i++)
    {
        cpu.memory[0x200 + i] = buffer[i]; // Remember program starts at 0x200 (512)
    }

    return cpu;
}

// Opcode types
#define OPCODE_CLEAR_SCREEN 0x00E0
#define OPCODE_JUMP 0x1000
#define OPCODE_SET_REG 0x6000
#define OPCODE_ADD_TO_REG 0x7000
#define OPCODE_SET_INDEX_REG 0xA000
#define OPCODE_DISPLAY 0xD000

// We do both at the same time because it is simpler, atleast for the CHIP-8
void DecodeAndExecute(chip8* cpu)
{
    switch (cpu->opcode & 0xF000)
    {
        case 0x0000: // 0NNN 
            switch (cpu->opcode)
            {
                case OPCODE_CLEAR_SCREEN:
                    // We clear the screen this way because it is a 2D array
                    printf("Clearing..\n");
                    for (int x=0; x<SCREEN_WIDTH; x++) { memset(cpu->display[x], 0, SCREEN_HEIGHT); }
                    break;
            }
            break;

        case OPCODE_JUMP:
            printf("Jumping...\n");
            cpu->pc = cpu->opcode & 0x0FFF; // Jump to target
            printf("Address: %x\n", cpu->opcode & 0x0FFF);
            break;

        case OPCODE_SET_REG:
            printf("Setting register...\n");
            cpu->V[(cpu->opcode & 0x0F00) >> 8] = (cpu->opcode & 0x00FF); // Bitshift to get value between 0 and F
            printf("Register: %x, Value: %x\n", (cpu->opcode & 0x0F00) >> 8, cpu->opcode & 0x00FF);
            break;

        case OPCODE_ADD_TO_REG:
            printf("Adding...\n");
            cpu->V[(cpu->opcode & 0x0F00) >> 8] += (cpu->opcode & 0x00FF);
            printf("Index: %x, Modifier: %x\n", (cpu->opcode & 0x0F00) >> 8, cpu->opcode & 0x00FF);
            break;

        case OPCODE_SET_INDEX_REG:
            printf("Setting index register...\n");
            cpu->I = cpu->opcode & 0x0FFF;
            printf("Index: %x\n", cpu->opcode & 0x0FFF);
            break;

        case OPCODE_DISPLAY:
            // THIS ONE IS STILL BROKEN :(
            cpu->VF = 0;
            printf("Displaying...\n");
            uint8_t x = cpu->V[(cpu->opcode & 0x0F00) >> 8] % SCREEN_WIDTH;
            uint8_t y = cpu->V[(cpu->opcode & 0x00F0) >> 4] % SCREEN_HEIGHT;
            uint8_t height = cpu->opcode & 0x000F;
            printf("Drawing at x: %d, y: %d. With a height of %d\n", x, y, height);

            for (int j=0; j<height; j++)
            {
                if (y+j>=SCREEN_HEIGHT) break;
                uint8_t spriteRow = cpu->memory[cpu->I+j];
                for (int i=0; i<8; i++)
                {
                    if (x+i>=SCREEN_WIDTH) break;
                    if (cpu->display[x+i][y+j]) cpu->VF = 1;
                    if ((spriteRow >> (i)) & 0x000F) cpu->display[x+i][y+j] = !cpu->display[x+i][y+j];
                }
            }
            
            break;
    }
}

void EmulateCycle(chip8* cpu)
{
    // Fetch instruction
    cpu->opcode = cpu->memory[cpu->pc] << 8 | cpu->memory[cpu->pc+1]; // Combine the two bytes to create the opcode
    printf("[0x%08x]: %04x\n", cpu->pc, cpu->opcode);
    cpu->pc+=2; // increment program counter by 2

    DecodeAndExecute(cpu);
}

int main( int argc, char* args[] )
{
    chip8 cpu = InitProgram("IBM Logo.ch8");
    InitWindow("CHIP-8", SCREEN_WIDTH*SCALE, SCREEN_HEIGHT*SCALE);

    SDL_Event e;
    int SDL_running = 1;
    
    while (SDL_running)
    {
        while(SDL_PollEvent(&e))
        {
            if(e.type==SDL_QUIT) SDL_running = 0; 
        }

        // Update input
        SDL_PumpEvents();
        cpu.keys = (char*)SDL_GetKeyboardState(NULL); // Casting to char* because SDL sucks and returns a const char? Not good practice

        EmulateCycle(&cpu);
        UpdateWindowDisplay(&cpu);
    }

    CloseWindow();
}
