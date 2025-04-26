#include <SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>

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
    uint8_t V[16]; // General purpose registers VF is also carry flag
    uint16_t I; // Index register
    uint16_t pc; // Program counter
    //uint8_t VF; // Carry flag

    uint16_t stack[16]; // We love the stack
    uint8_t sp; // Stack pointer

    char* keys; // Input (would be an array of 16 but a pointer works better with SDL)

    uint8_t delayTimer;
    uint8_t soundTimer;

    uint8_t halted;

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

// Keys
SDL_Scancode SDL_inputs[16] = {
    SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
    SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R,
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
    SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V,
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
#define OPCODE_NO_ARGS 0x0000 // Opcodes with no arguments
    #define OPCODE_CLEAR_SCREEN 0x00E0 // Clear the screen

#define OPCODE_RETURN_SUBROUTINE 0x00EE // Returns from subroutine/function
#define OPCODE_JUMP 0x1000 // Jumps to position
#define OPCODE_CALL_SUBROUTINE 0x2000 // Calls subroutine/function

#define OPCODE_REG_IS_VALUE 0x3000 // If register is equal to value
#define OPCODE_REG_IS_NOT_VALUE 0x4000 // If register is not equal to value
#define OPCODE_REG_IS_REG 0x5000 // If register is equal to other register
#define OPCODE_REG_IS_NOT_REG 0x9000 // If register is not equal to other registers

#define OPCODE_SET_REG 0x6000 // Set register to value
#define OPCODE_ADD_TO_REG 0x7000 // Add value to register
#define OPCODE_SET_INDEX_REG 0xA000 // Set index register
#define OPCODE_JUMP_OFFSET 0xB000 // Jumps with the offset of V0 (COSMAC VIP implementation)
#define OPCODE_RANDOM 0xC000 // Sets VX to a random number binary ANDed with NN
#define OPCODE_DISPLAY 0xD000 // Draw sprite

#define OPCODE_ARITHMETIC 0x8000 // Various logic and arithmetic opcodes 
    #define OPCODE_SET 0x0 // VX is set to the value of VY
    #define OPCODE_BINARY_OR 0x1 // VX is set to the binary OR of VX and VY
    #define OPCODE_BINARY_AND 0x2 // VX is set to the binary AND of VX and VY
    #define OPCODE_LOGICAL_XOR 0x3 // VX is set to the XOR of VX and VY
    #define OPCODE_ADD 0x4 // VX is set to VX + VY
    #define OPCODE_SUBTRACT_XY 0x5 // VX is set to VX - VY
    #define OPCODE_SUBTRACT_YX 0x7 // VX is set to VY - VX
    #define OPCODE_SHIFT_RIGHT 0x6 // Sets VX to VY and shifts VX to the right (We are using the COSMAC VIP implementation for now)
    #define OPCODE_SHIFT_LEFT 0xE // Like OPCODE_SHIFT_RIGHT but we shift left

#define OPCODE_KEY_SKIP 0xE000
    #define OPCODE_SKIP_IF_KEY 0x9E
    #define OPCODE_SKIP_IF_NOT_KEY 0xA1

#define OPCODE_F 0xF000 // Group of misc opcodes
    #define OPCODE_STORE_MEMORY 0x55 // Stores registers to memory
    #define OPCODE_LOAD_MEMORY 0x65 // Loads registers from memory
    #define OPCODE_CONVERT_DECIMAL 0x33 // Finds the 3 decimal digits of VX and stores it in memory
    #define OPCODE_ADD_TO_INDEX 0x1E // Adds VX to index
    #define OPCODE_GET_DELAY_TIMER 0x07 // Sets VX to current value of delay delayTimer
    #define OPCODE_SET_DELAY_TIMER 0x15 // Sets delayTimer to VX
    #define OPCODE_SET_SOUND_TIMER 0x18 // Sets soundTimer to VX
    #define OPCODE_AWAIT_KEY 0x0A // Traps program in loop until key pressed

#define OPCODE_X(opcode) ((opcode & 0x0F00) >> 8)
#define OPCODE_Y(opcode) ((opcode & 0x00F0) >> 4)
#define OPCODE_NNN(opcode) (opcode & 0x0FFF)
#define OPCODE_NN(opcode) (opcode & 0x00FF)
#define OPCODE_N(opcode) (opcode & 0x000F)

// We do both at the same time because it is simpler, atleast for the CHIP-8
void DecodeAndExecute(chip8* cpu)
{
    switch (cpu->opcode & 0xF000)
    {
        case 0x0000: // 0NNN 
            switch (cpu->opcode)
            {
                case OPCODE_CLEAR_SCREEN:
                {
                    // We clear the screen this way because it is a 2D array
                    for (int x=0; x<SCREEN_WIDTH; x++) { memset(cpu->display[x], 0, SCREEN_HEIGHT); }
                } break;

                case OPCODE_RETURN_SUBROUTINE:
                {
                    if (cpu->sp == 0)
                    {
                        printf("[WARNING]: Stack is empty. Ignoring instruction.\n");
                        break;
                    }
                    cpu->sp--;
                    cpu->pc = cpu->stack[cpu->sp];
                } break;

                default:
                {
                    printf("[ERROR]: Invalid opcode: '%04x'\n", cpu->opcode);
                    cpu->halted = 1;
                } break;
            } break;

        case OPCODE_ARITHMETIC:
        {
            switch (OPCODE_N(cpu->opcode))
            {
                case OPCODE_SET:
                { cpu->V[OPCODE_X(cpu->opcode)] = cpu->V[OPCODE_Y(cpu->opcode)]; } break;

                case OPCODE_BINARY_OR:
                { cpu->V[OPCODE_X(cpu->opcode)] |= cpu->V[OPCODE_Y(cpu->opcode)]; } break;

                case OPCODE_BINARY_AND:
                { cpu->V[OPCODE_X(cpu->opcode)] &= cpu->V[OPCODE_Y(cpu->opcode)]; } break;

                case OPCODE_LOGICAL_XOR:
                { cpu->V[OPCODE_X(cpu->opcode)] ^= cpu->V[OPCODE_Y(cpu->opcode)]; } break;

                case OPCODE_ADD:
                {
                    int vX = cpu->V[OPCODE_X(cpu->opcode)];
                    int vY = cpu->V[OPCODE_Y(cpu->opcode)];

                    cpu->V[OPCODE_X(cpu->opcode)] += cpu->V[OPCODE_Y(cpu->opcode)];

                    cpu->V[0xF] = (vX + vY > 255) ? 1 : 0; // Check for overflow

                } break;

                case OPCODE_SUBTRACT_XY:
                {
                    uint8_t vX = cpu->V[OPCODE_X(cpu->opcode)];
                    uint8_t vY = cpu->V[OPCODE_Y(cpu->opcode)];

                    cpu->V[OPCODE_X(cpu->opcode)] = vX - vY;

                    cpu->V[0xF] = (vY > vX) ? 0 : 1;

                } break;

                case OPCODE_SUBTRACT_YX:
                {
                    uint8_t vX = cpu->V[OPCODE_X(cpu->opcode)];
                    uint8_t vY = cpu->V[OPCODE_Y(cpu->opcode)];

                    cpu->V[OPCODE_X(cpu->opcode)] = vY - vX;

                    cpu->V[0xF] = (vX > vY) ? 0 : 1;

                } break;

                case OPCODE_SHIFT_RIGHT:
                {
                    cpu->V[OPCODE_X(cpu->opcode)] = cpu->V[OPCODE_Y(cpu->opcode)];
                    uint8_t removedBit = cpu->V[OPCODE_X(cpu->opcode)] & 0b00000001;

                    cpu->V[OPCODE_X(cpu->opcode)] >>= 1;

                    cpu->V[0xF] = removedBit; 
 
                } break;

                case OPCODE_SHIFT_LEFT:
                {
                    cpu->V[OPCODE_X(cpu->opcode)] = cpu->V[OPCODE_Y(cpu->opcode)];
                    uint8_t removedBit = (cpu->V[OPCODE_X(cpu->opcode)] & 0b10000000) >> 7;

                    cpu->V[OPCODE_X(cpu->opcode)] <<= 1;

                    cpu->V[0xF] = removedBit; 
                } break;

                default:
                {
                    printf("[ERROR]: Invalid opcode: '%04x'\n", cpu->opcode);
                    cpu->halted = 1;
                } break;

            } break;
        } break;

        case OPCODE_JUMP:
            { cpu->pc = OPCODE_NNN(cpu->opcode); /* Jump to target */ } break;

        case OPCODE_RANDOM:
            { cpu->V[OPCODE_X(cpu->opcode)] = (rand() % 255) & OPCODE_NN(cpu->opcode); } break;

        case OPCODE_CALL_SUBROUTINE:
        {
            uint16_t prevPos = cpu->pc; // Store previous pc position

            // Push previous position to stack
            if (cpu->sp >=16) // Not if there is no space
            {
                printf("[ERROR]: Stack overflow.\n");
                cpu->halted = 1;
            }
            cpu->stack[cpu->sp] = prevPos;
            cpu->sp++;

            cpu->pc = OPCODE_NNN(cpu->opcode); // Jump 2.0
        } break;

        case OPCODE_REG_IS_VALUE:
        {
            if (cpu->V[OPCODE_X(cpu->opcode)] == OPCODE_NN(cpu->opcode)) cpu->pc+=2;
        } break;

        case OPCODE_REG_IS_NOT_VALUE:
            { if (cpu->V[OPCODE_X(cpu->opcode)] != OPCODE_NN(cpu->opcode)) cpu->pc+=2; } break;

        case OPCODE_REG_IS_REG:
            { if (cpu->V[OPCODE_X(cpu->opcode)] == cpu->V[OPCODE_Y(cpu->opcode)]) cpu->pc+=2; } break;

        case OPCODE_REG_IS_NOT_REG:
            { if (cpu->V[OPCODE_X(cpu->opcode)] != cpu->V[OPCODE_Y(cpu->opcode)]) cpu->pc+=2; } break;

        case OPCODE_SET_REG:
            { cpu->V[OPCODE_X(cpu->opcode)] = OPCODE_NN(cpu->opcode); /* Bitshift to get value between 0 and F */ } break;

        case OPCODE_ADD_TO_REG:
            { cpu->V[OPCODE_X(cpu->opcode)] += OPCODE_NN(cpu->opcode); } break;

        case OPCODE_SET_INDEX_REG:
             { cpu->I = OPCODE_NNN(cpu->opcode); } break;

        case OPCODE_JUMP_OFFSET:
            { cpu->pc = OPCODE_NNN(cpu->opcode) + cpu->V[0]; } break;

        case OPCODE_F:
        {
            switch (OPCODE_NN(cpu->opcode))
            {
                case OPCODE_STORE_MEMORY:
                {
                    for (int i=0; i<=OPCODE_X(cpu->opcode); i++) cpu->memory[cpu->I + i] = cpu->V[i];
                } break;

                case OPCODE_LOAD_MEMORY:
                {
                    for (int i=0; i<=OPCODE_X(cpu->opcode); i++) cpu->V[i] = cpu->memory[cpu->I + i];
                } break;

                case OPCODE_CONVERT_DECIMAL:
                {
                    cpu->memory[cpu->I] = cpu->V[OPCODE_X(cpu->opcode)] / 100;
                    cpu->memory[cpu->I+1] = (cpu->V[OPCODE_X(cpu->opcode)] / 10) %10;
                    cpu->memory[cpu->I+2] = cpu->V[OPCODE_X(cpu->opcode)] % 10;
                } break;

                case OPCODE_ADD_TO_INDEX:
                {
                    cpu->I += cpu->V[OPCODE_X(cpu->opcode)];
                } break;

                case OPCODE_GET_DELAY_TIMER:
                {
                    cpu->V[OPCODE_X(cpu->opcode)] = cpu->delayTimer;
                } break;

                case OPCODE_SET_DELAY_TIMER:
                {
                    cpu->delayTimer = cpu->V[OPCODE_X(cpu->opcode)];
                } break;

                case OPCODE_SET_SOUND_TIMER:
                {
                    cpu->soundTimer = cpu->V[OPCODE_X(cpu->opcode)];
                } break;

                case OPCODE_AWAIT_KEY:
                {
                    printf("a");
                    for (int i=0; i<16; i++)
                    {
                        if (cpu->keys[SDL_inputs[i]])
                        {
                            cpu->V[OPCODE_X(cpu->opcode)] = i;
                            cpu->pc+=2;            
                            break;
                        }
                    }
                    cpu->pc-=2;
                } break;

                default:
                {
                    printf("[ERROR]: Invalid opcode: '%04x'\n", cpu->opcode);
                    cpu->halted = 1;
                } break;
            }
        } break;

        case OPCODE_KEY_SKIP:
        {
            switch (OPCODE_NN(cpu->opcode))
            {
                case OPCODE_SKIP_IF_KEY:
                    { if (cpu->keys[SDL_inputs[OPCODE_X(cpu->opcode)]]) cpu->pc++; } break;

                case OPCODE_SKIP_IF_NOT_KEY:
                    { if (!(cpu->keys[SDL_inputs[OPCODE_X(cpu->opcode)]])) cpu->pc++; } break;
            } 
        } break;

        case OPCODE_DISPLAY:
        {
            cpu->V[0xF] = 0;
            uint8_t x = cpu->V[OPCODE_X(cpu->opcode)] % SCREEN_WIDTH;
            uint8_t y = cpu->V[OPCODE_Y(cpu->opcode)] % SCREEN_HEIGHT;
            uint8_t height = OPCODE_N(cpu->opcode);

            for (int j=0; j<height; j++)
            {
                if (y+j>=SCREEN_HEIGHT) break;
                uint8_t spriteRow = cpu->memory[cpu->I+j];
                for (int i=0; i<8; i++)
                {
                    if (x+i>=SCREEN_WIDTH) break;
                    if (cpu->display[x+i][y+j]) cpu->V[0xF] = 1;
                    if ((spriteRow << i) & 0b10000000) cpu->display[x+i][y+j] = !cpu->display[x+i][y+j];
                }
            }
        } break;

        default:
        {
            printf("[ERROR]: Invalid opcode: '%04x'\n", cpu->opcode);
            cpu->halted = 1;
        } break;
    }
}

void EmulateCycle(chip8* cpu)
{
    if (cpu->delayTimer>0) cpu->delayTimer--;
    if (cpu->soundTimer>0)
    {
        printf("BEEP!\n");
        cpu->delayTimer--;
    }

    // Fetch instruction
    cpu->opcode = cpu->memory[cpu->pc] << 8 | cpu->memory[cpu->pc+1]; // Combine the two bytes to create the opcode
    printf("[0x%08x]: %04x\n", cpu->pc, cpu->opcode);
    cpu->pc+=2; // increment program counter by 2

    DecodeAndExecute(cpu);
}

int main( int argc, char* args[] )
{
    // Error handling yippe :D
    if (argc <= 1)
    {
        printf("[ERROR]: Not enough arguments.\n");
        return -1;
    }
    else if (argc > 2) {
        printf("[WARNING]: Too many arguments. Only using the first one.\n");
    }
    if (strlen(args[1]) <= 4)
    {
        printf("[ERROR]: Invalid file name. Must be more than 4 characters long.\n");
        return -1;
    }
    if (strcmp(((char*)args[1] + strlen(args[1])-4), ".ch8"))
    {
        printf("[ERROR]: Invalid filetype. File must have '.ch8' extension.");
        return -1;
    }


    chip8 cpu = InitProgram(args[1]);
    InitWindow("CHIP-8", SCREEN_WIDTH*SCALE, SCREEN_HEIGHT*SCALE);

    SDL_Event e;
    cpu.halted = 0;

    while (!cpu.halted)
    {
        while(SDL_PollEvent(&e))
        {
            if(e.type==SDL_QUIT) cpu.halted = 1; 
        }

        // Update multiple times a frame to speed up program
        for (int i=0; i<5; i++)
        {
            // Update input
            SDL_PumpEvents();
            cpu.keys = (char*)SDL_GetKeyboardState(NULL); // Casting to char* because SDL sucks and returns a const char? Not good practice

            EmulateCycle(&cpu);
        }
    
        UpdateWindowDisplay(&cpu);
    }

    CloseWindow();
}
