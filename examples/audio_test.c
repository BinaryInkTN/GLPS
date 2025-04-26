#include <GLPS/glps_audio_stream.h>
#include <GLPS/glps_timer.h>

int main()
{
    glps_timer timer;
    glps_timer_start(&timer);
    glps_audio_stream *am = glps_audio_stream_init("default",
                                                   4096, 44100, 2, 16, 4096);
    glps_audio_stream_play(am, "./test.mp3", 44100, 2, 16, 4096);
    sleep(5);

    glps_timer_stop(&timer);

    
    double elapsed = glps_timer_elapsed_ms(&timer);
    printf("%lf", elapsed);
    return 0;
}