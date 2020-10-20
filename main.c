#include <SDL.h>
#include <limits.h>
#include <stdbool.h>
#include <time.h>

#define AUDIO_SAMPLING_RATE 44100
#define AUDIO_AMPLITUDE (0.025 * SHRT_MAX) // volume

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#define ROUND_MAX_SCORE 11
#define ROUND_OVER_TIMEOUT 6 // seconds

#define PADDLE_WIDTH 10
#define PADDLE_HEIGHT 50
#define PADDLE_SCREEN_MARGIN_X 50
#define PADDLE_SPEED 500 // px per second

#define BALL_SIZE 14
#define BALL_SPEED_Y 300          // px per second
#define BALL_SPEED_INITIAL_X 300  // px per second
#define BALL_SPEED_MAX_X 500      // px per second
#define BALL_SPEED_INCREMENT_X 20 // px per second
#define BALL_SERVE_DELAY 2        // seconds

#define SCORE_Y 50
#define SCORE_SCREEN_MARGIN_X 150
#define SCORE_DIGIT_SCALE_FACTOR 10

#define NET_WIDTH 5
#define NET_HEIGHT 15

struct audio_clip {
    short *samples;
    size_t samples_size;
};

struct audio {
    SDL_AudioDeviceID device_id;
    struct audio_clip score;
    struct audio_clip paddle_hit;
    struct audio_clip bounce;
};

struct digits_image {
    SDL_Texture *texture;
    SDL_Rect digit_size;
};

struct paddle {
    int no;
    float y;
    float velocity;
    SDL_Rect rect;
    int score;
};

struct ball {
    float x, y;
    SDL_FPoint velocity;
    SDL_Rect rect;
    float serve_delay;
};

struct game {
    struct paddle paddle_1;
    struct paddle paddle_2;
    struct ball ball;
    float round_over_timeout;
};

int clamp(int x, int min, int max) {
    return x < min ? min : x > max ? max : x;
}

int randrange(int min, int max) {
    return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

struct audio_clip make_square_wave(int freq, float duration) {
    int num_samples = duration * AUDIO_SAMPLING_RATE;
    size_t size = num_samples * sizeof(short);
    short *samples = malloc(size);

    for (int i = 0; i < num_samples; i++) {
        if (sin(2 * M_PI * freq * (i / (double)AUDIO_SAMPLING_RATE)) >= 0) {
            samples[i] = AUDIO_AMPLITUDE;
        } else {
            samples[i] = -AUDIO_AMPLITUDE;
        }
    }

    return (struct audio_clip){samples, size};
}

void queue_audio_clip(struct audio *audio, struct audio_clip *clip) {
    SDL_QueueAudio(audio->device_id, clip->samples, clip->samples_size);
}

struct paddle make_paddle(int no) {
    struct paddle paddle = {
        .no = no,
        .y = (WINDOW_HEIGHT - PADDLE_HEIGHT) / 2,
        .rect = {
            .x = no == 1 ? PADDLE_SCREEN_MARGIN_X
                         : WINDOW_WIDTH - PADDLE_SCREEN_MARGIN_X,
            .w = PADDLE_WIDTH,
            .h = PADDLE_HEIGHT,
        }};
    paddle.rect.y = roundf(paddle.y);
    return paddle;
}

struct ball make_ball() {
    return (struct ball){.rect = {.w = BALL_SIZE, .h = BALL_SIZE}};
}

// Position the ball on one of the sides of the net and change its velocity
// based on which paddle it is being served to.
void serve_ball(struct ball *ball, int paddle_no, bool round_over) {
    if (paddle_no == 1) {
        ball->x = ((WINDOW_WIDTH - BALL_SIZE) / 2) - NET_WIDTH * 2;
        ball->velocity.x = -BALL_SPEED_INITIAL_X;
    } else {
        ball->x = ((WINDOW_WIDTH - BALL_SIZE) / 2) + NET_WIDTH * 2;
        ball->velocity.x = BALL_SPEED_INITIAL_X;
    }

    ball->y = randrange(0, WINDOW_HEIGHT - BALL_SIZE);

    ball->rect.x = roundf(ball->x);
    ball->rect.y = roundf(ball->y);

    ball->velocity.y = randrange(-BALL_SPEED_Y, BALL_SPEED_Y);

    if (!round_over) {
        ball->serve_delay = BALL_SERVE_DELAY;
    }
}

// Set the vertical velocity of the paddles based on the state of the paddle
// controls.
void check_paddle_controls(struct paddle *paddle_1, struct paddle *paddle_2) {
    const uint8_t *state = SDL_GetKeyboardState(NULL);

    if (state[SDL_SCANCODE_W]) {
        paddle_1->velocity = -PADDLE_SPEED;
    } else if (state[SDL_SCANCODE_S]) {
        paddle_1->velocity = PADDLE_SPEED;
    } else {
        paddle_1->velocity = 0;
    }

    if (state[SDL_SCANCODE_UP]) {
        paddle_2->velocity = -PADDLE_SPEED;
    } else if (state[SDL_SCANCODE_DOWN]) {
        paddle_2->velocity = PADDLE_SPEED;
    } else {
        paddle_2->velocity = 0;
    }
}

void update_paddle(struct paddle *paddle, double elapsed_time) {
    paddle->y += paddle->velocity * elapsed_time;
    paddle->y = clamp(paddle->y, 0, WINDOW_HEIGHT - PADDLE_HEIGHT);
    paddle->rect.y = roundf(paddle->y);
}

void update_ball(struct game *game, struct audio *audio, struct ball *ball,
                 double elapsed_time) {
    if (ball->serve_delay > 0) {
        ball->serve_delay -= elapsed_time;
        if (ball->serve_delay < 0) {
            ball->serve_delay = 0;
        }
        return;
    }

    ball->x += ball->velocity.x * elapsed_time;
    ball->y += ball->velocity.y * elapsed_time;

    // The ball will always bounce off vertical walls.
    if (ball->y < 0 || ball->y + BALL_SIZE > WINDOW_HEIGHT) {
        ball->velocity.y *= -1;
        if (game->round_over_timeout == 0) {
            queue_audio_clip(audio, &audio->bounce);
        }
    }

    // The ball will only bounce off horizontal walls when the game is over.
    if (game->round_over_timeout != 0) {
        if (ball->x < 0 || ball->x + BALL_SIZE > WINDOW_WIDTH) {
            ball->velocity.x *= -1;
        }

        ball->x = clamp(ball->x, 0, WINDOW_WIDTH - BALL_SIZE);
    }

    ball->y = clamp(ball->y, 0, WINDOW_HEIGHT - BALL_SIZE);

    ball->rect.x = roundf(ball->x);
    ball->rect.y = roundf(ball->y);
}

// Set the velocity of the ball based on which paddle it is being returned to
// and where it hit the given paddle.
void return_ball(struct ball *ball, struct paddle *paddle) {
    if (paddle->no == 1) {
        ball->x = paddle->rect.x + paddle->rect.w;
    } else {
        ball->x = paddle->rect.x - ball->rect.w;
    }

    ball->velocity.x *= -1;

    if (fabs(ball->velocity.x) < BALL_SPEED_MAX_X) {
        if (ball->velocity.x > 0) {
            ball->velocity.x += BALL_SPEED_INCREMENT_X;
        } else {
            ball->velocity.x += -BALL_SPEED_INCREMENT_X;
        }
    }

    float intersect_y =
        (ball->y + ball->rect.h - paddle->y) / (paddle->rect.h + ball->rect.h);

    ball->velocity.y += (0.5 - intersect_y) * BALL_SPEED_Y * 2;

    if (ball->velocity.y > BALL_SPEED_Y) {
        ball->velocity.y = BALL_SPEED_Y;
    } else if (ball->velocity.y < -BALL_SPEED_Y) {
        ball->velocity.y = -BALL_SPEED_Y;
    }
}

// Return the ball in the other paddle's direction if it hit a paddle.
void check_paddle_hit_ball(struct game *game, struct audio *audio) {
    if (game->round_over_timeout > 0) {
        return;
    }

    if (SDL_HasIntersection(&game->paddle_1.rect, &game->ball.rect)) {
        return_ball(&game->ball, &game->paddle_1);
        queue_audio_clip(audio, &audio->paddle_hit);
    } else if (SDL_HasIntersection(&game->paddle_2.rect, &game->ball.rect)) {
        return_ball(&game->ball, &game->paddle_2);
        queue_audio_clip(audio, &audio->paddle_hit);
    }
}

// Score a point for a paddle if the other paddle missed returning the ball.
void check_paddle_miss_ball(struct game *game, struct audio *audio) {
    if (game->ball.rect.x + game->ball.rect.w < 0) {
        if (game->paddle_2.score == ROUND_MAX_SCORE - 1) {
            serve_ball(&game->ball, 2, true);
        } else {
            serve_ball(&game->ball, 1, false);
            queue_audio_clip(audio, &audio->score);
        }
        game->paddle_2.score++;
    } else if (game->ball.rect.x > WINDOW_WIDTH) {
        if (game->paddle_1.score == ROUND_MAX_SCORE - 1) {
            serve_ball(&game->ball, 1, true);
        } else {
            serve_ball(&game->ball, 2, false);
            queue_audio_clip(audio, &audio->score);
        }
        game->paddle_1.score++;
    }
}

// Start the round over timeout when one of the paddles reaches the max score,
// update the round over timeout and restart the round when the timeout is over.
void check_round_over(struct game *game, double elapsed_time) {
    if (game->round_over_timeout == 0) {
        if (game->paddle_1.score == ROUND_MAX_SCORE ||
            game->paddle_2.score == ROUND_MAX_SCORE) {
            game->round_over_timeout = ROUND_OVER_TIMEOUT;
        }
    } else {
        game->round_over_timeout -= elapsed_time;
        if (game->round_over_timeout <= 0) {
            game->paddle_1.score = 0;
            game->paddle_2.score = 0;
            serve_ball(&game->ball, randrange(1, 2), false);
            game->round_over_timeout = 0;
        }
    }
}

// Render the score of a paddle using the digits in the digits image.
void render_paddle_score(SDL_Renderer *renderer, struct digits_image digits,
                         struct paddle paddle) {
    SDL_Rect src = digits.digit_size;

    SDL_Rect dest = {.x = paddle.no == 1
                              ? (WINDOW_WIDTH / 2) - SCORE_SCREEN_MARGIN_X
                              : WINDOW_WIDTH - SCORE_SCREEN_MARGIN_X,
                     .y = SCORE_Y,
                     .w = src.w * SCORE_DIGIT_SCALE_FACTOR,
                     .h = src.h * SCORE_DIGIT_SCALE_FACTOR};

    if (paddle.score == 0) {
        SDL_RenderCopy(renderer, digits.texture, &src, &dest);
        return;
    }

    int n = paddle.score;
    while (n != 0) {
        src.x = (n % 10) * src.w;
        SDL_RenderCopy(renderer, digits.texture, &src, &dest);
        dest.x -= SCORE_DIGIT_SCALE_FACTOR * src.w * 2;
        n /= 10;
    }
}

// Render the table tennis net in the middle of the screen with small filled
// rectangles.
void render_net(SDL_Renderer *renderer) {
    for (int y = 0; y < WINDOW_HEIGHT; y += NET_HEIGHT * 2) {
        SDL_RenderFillRect(renderer,
                           &(SDL_Rect){.x = (WINDOW_WIDTH - NET_WIDTH) / 2,
                                       .y = y,
                                       .w = NET_WIDTH,
                                       .h = NET_HEIGHT});
    }
}

// Render paddle as a filled rectangle.
void render_paddle(SDL_Renderer *renderer, struct game *game,
                   struct paddle paddle) {
    if (game->round_over_timeout == 0) {
        SDL_RenderFillRect(renderer, &paddle.rect);
    }
}

// Render ball as a filled rectangle.
void render_ball(SDL_Renderer *renderer, struct ball ball) {
    if (ball.serve_delay == 0) {
        SDL_RenderFillRect(renderer, &ball.rect);
    }
}

int main() {
    struct audio audio = {
        .score = make_square_wave(240, 0.510),
        .paddle_hit = make_square_wave(480, 0.035),
        .bounce = make_square_wave(240, 0.020),
    };

    // For choosing which paddle gets served the ball first and the vertical
    // position of the ball every time it appears.
    srand(time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Initialize SDL: %s",
                     SDL_GetError());
        return EXIT_FAILURE;
    }

    // The audio subsystem is not required.
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Initialize SDL audio subsystem: %s", SDL_GetError());
    }

    // Open an audio device for playing signed 16-bit mono samples and ignore if
    // no device can be opened.
    audio.device_id =
        SDL_OpenAudioDevice(NULL, 0,
                            &(SDL_AudioSpec){.freq = AUDIO_SAMPLING_RATE,
                                             .format = AUDIO_S16SYS,
                                             .channels = 1,
                                             .samples = 2048},
                            NULL, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    if (audio.device_id == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Open audio device: %s",
                     SDL_GetError());
    }

    // Unpause the audio device which is paused by default.
    SDL_PauseAudioDevice(audio.device_id, 0);

    // Create a hidden window so it may only be shown after the game is
    // initialized.
    SDL_Window *window = SDL_CreateWindow(
        "Pong", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WINDOW_WIDTH,
        WINDOW_HEIGHT, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Create window: %s",
                     SDL_GetError());
        return EXIT_FAILURE;
    }

    // Synchronizing the renderer presentation to the screen's refresh rate
    // helps alleviate stuttering.
    SDL_Renderer *renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Create renderer: %s",
                     SDL_GetError());
        return EXIT_FAILURE;
    }

    // So that handling different resolutions and mantaining the aspect
    // ratio in the given resolution isn't necessary.
    SDL_RenderSetLogicalSize(renderer, WINDOW_WIDTH, WINDOW_HEIGHT);

    // Load the digits for rendering the paddles score.
    SDL_Surface *digits_surf = SDL_LoadBMP("digits.bmp");
    if (digits_surf == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Load image: %s",
                     SDL_GetError());
        return EXIT_FAILURE;
    }

    // Uncomment to make the black pixels transparent in case the game's
    // background is made to be a color other than black.
    // SDL_SetColorKey(digits_surf, SDL_TRUE,
    //                 SDL_MapRGB(digits_surf->format, 0, 0, 0));

    SDL_Texture *digits_tex =
        SDL_CreateTextureFromSurface(renderer, digits_surf);
    if (digits_tex == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Create texture: %s",
                     SDL_GetError());
        return EXIT_FAILURE;
    }

    struct digits_image digits = {
        .digit_size = {.w = digits_surf->w / 10, .h = digits_surf->h},
        .texture = digits_tex,
    };

    // Show the window after the game is done initializing.
    SDL_ShowWindow(window);

    bool running = true;

    uint64_t counter_time = SDL_GetPerformanceCounter();

    struct game game = {.paddle_1 = make_paddle(1),
                        .paddle_2 = make_paddle(2),
                        .ball = make_ball()};

    serve_ball(&game.ball, randrange(1, 2), false);

    while (running) {
        // Calculate the difference of time between the last frame and the
        // current frame for the purpose of mantaining a consistent game
        // speed regardless of how fast or slow frames are being drawn.
        uint64_t last_counter_time = counter_time;
        counter_time = SDL_GetPerformanceCounter();
        double elapsed_time = (counter_time - last_counter_time) /
                              (double)SDL_GetPerformanceFrequency();

        // Poll events and handle quitting, toggling fullscreen and changing
        // the score when debugging.
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                case SDLK_F11:
                    if (SDL_GetWindowFlags(window) &
                        SDL_WINDOW_FULLSCREEN_DESKTOP) {
                        SDL_SetWindowFullscreen(window, 0);
                    } else {
                        SDL_SetWindowFullscreen(window,
                                                SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                    break;
#ifdef CHEATS
                case SDLK_1:
                    game.paddle_1.score += 1;
                    break;
                case SDLK_2:
                    game.paddle_2.score += 1;
                    break;
#endif
                }
                break;
            }
        }

        // Begin to update the game state.
        check_paddle_controls(&game.paddle_1, &game.paddle_2);

        update_paddle(&game.paddle_1, elapsed_time);
        update_paddle(&game.paddle_2, elapsed_time);
        update_ball(&game, &audio, &game.ball, elapsed_time);

        check_paddle_miss_ball(&game, &audio);
        check_paddle_hit_ball(&game, &audio);

        check_round_over(&game, elapsed_time);

        // Clear the renderer with black.
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        render_paddle_score(renderer, digits, game.paddle_1);
        render_paddle_score(renderer, digits, game.paddle_2);

        // Begin drawing the game with white.
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        render_net(renderer);
        render_paddle(renderer, &game, game.paddle_1);
        render_paddle(renderer, &game, game.paddle_2);
        render_ball(renderer, game.ball);

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(digits_tex);
    SDL_FreeSurface(digits_surf);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    SDL_CloseAudioDevice(audio.device_id);

    SDL_Quit();

    free(audio.score.samples);
    free(audio.paddle_hit.samples);
    free(audio.bounce.samples);

    return EXIT_SUCCESS;
}
