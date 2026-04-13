Для создания собственного пункта в menuconfig в ESP-IDF 5.5 используется система Kconfig. Чтобы добавить настройки, которые будут отображаться при выполнении команды idf.py menuconfig, вам нужно создать или отредактировать файл с именем Kconfig или Kconfig.projbuild.
В каталоге  "main/" вашего проекта создайте текстовый файл:
- Kconfig.projbuild: Используйте это имя, если хотите, чтобы настройки отображались в главном меню (Top-level).
- Kconfig: Используйте это имя внутри папки компонента, чтобы настройки появились в подразделе "Component config".

```c
menu "My Custom Settings"
    config MY_DEVICE_ID
        int "Device ID"
        default 1
        help
            Укажите уникальный ID вашего устройства (0-255).

    config MY_SERVER_URL
        string "Server URL"
        default "http://example.com"
        help
            Адрес сервера для отправки данных.
endmenu
```

После сохранения файла и запуска idf.py menuconfig, ваши настройки попадут в файл заголовка sdkconfig.h. В коде они будут доступны с префиксом CONFIG_
```c
#include "sdkconfig.h"

    int device_id = CONFIG_MY_DEVICE_ID;
    const char* url = CONFIG_MY_SERVER_URL;

```

Основные типы параметров:
- bool: Галочка (вкл/выкл). В коде будет 1 или не определено.
- int: Целое число.
- string: Текстовая строка.
- choice: Выпадающий список для выбора одного варианта из нескольких.



Список "безопасных" пинов для входов (ESP32)
Самые безопасные (Input/Output):
GPIO 4, 5, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33
Только вход (Input Only / Sensor Pins):
GPIO 34, 35, 36, 39.












