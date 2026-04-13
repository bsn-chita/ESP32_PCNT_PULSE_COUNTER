Разделение кода на модули.

Include Guards (защита от повторного включения). Стандарт де-факто для заголовочных файлов в Си.

В начале .h файла добавьте

Например, для файла pcnt_encoder.h ( PCNT_ENCODER_H )

```c
#ifndef PCNT_ENCODER_H
#define PCNT_ENCODER_H

#include "driver/pulse_cnt.h"

// ... весь ваш код (extern, прототипы функций) ...

#endif
```

Или, в современном стиле, можно использовать одну короткую строку в самом верху файла:

```c
#pragma once

#include "driver/pulse_cnt.h"
// ...
```
