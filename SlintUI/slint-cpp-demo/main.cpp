#include "app_window.h" // Plik automatycznie wygenerowany przez Slint i CMake

int main(int argc, char **argv) {
    // Tworzenie instancji naszego okna
    auto main_window = AppWindow::create();

    // Reakcja na kliknięcie przycisku
    main_window->on_button_clicked([&]() {
        main_window->set_message("Silnik graficzny z GitHuba dziala super!");
    });

    // Uruchomienie pętli głównej interfejsu
    main_window->run();
    return 0;
}
