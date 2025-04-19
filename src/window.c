
void InitWindow(char* title, int width, int height) // Place to toss in SDL functions
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        exit(-1);
    }

    SDL_state.window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_SHOWN);
    if (SDL_state.window == NULL)
    {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(-1);
    }

    SDL_state.renderer = SDL_CreateRenderer(SDL_state.window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (SDL_state.window == NULL)
    {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(-1);
    }

    SDL_state.texture = SDL_CreateTexture(SDL_state.renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (SDL_state.texture == NULL)
    {
        printf("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(-1);
    } 
}


void CloseWindow() { SDL_DestroyTexture(SDL_state.texture);
  SDL_DestroyRenderer(SDL_state.renderer); SDL_DestroyWindow(SDL_state.window);
  SDL_Quit(); }
