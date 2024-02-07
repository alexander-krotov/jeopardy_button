# Hardware and software for Jeopardy-like games

Hardware: control board is baed on EP32 SOC module WROOM-32. It features 7-segment display to show the players number, small-size speaker for sound signals and few leds to indicate the player, false-start or corret timing signal.
It allows 4 players, each player has his own "console" (push-button and signaling led). Consoles are conected to the control board with phone RJ- connectors and phone wires.

Hardware schematics placed at https://oshwlab.com/alexander.krotov/esp32-knopki

# Железо и софт для "Своей игры"

Железо: основной блок постоен на ESP32 модуле (вариант WROOM-32). На блоке есть 7-сегментный индикатор чтобы показывать номер нажавшего игрока, малогабаритная пищалка для звуковых сигналов, зеленый и красный светодиоды для индикации фальшстарта или попадания во время, 4 светодидода дублирующие индикацию для выигравшего игрока. "Консольки" игроков сотоят из кнопки и светодида (дополнительная индикация), подключаются через телефонные разъемы телефонными же проводами.
"Консоль" ведущего представляет из себя три кнопки для сброса начальное состояние и запуск таймеров на 7 и 20 секунд.
На основной плате предусмотрены также выводы для подключения двху внешних светодионых индикаторов и lcd-диспплея (если когда-нибудь понадобится написать для него софт).
Схему можно найти здесь https://oshwlab.com/alexander.krotov/esp32-knopki

Софт запускает точку доступа Wifi, по ней можно настраивать ситстему.



