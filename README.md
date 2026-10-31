# ESP32 Data Storage — HTTP API

Веб-сервис на ESP32 (порт **80**), работающий с SD-картами по SPI. Все данные отдаются/принимаются по HTTP: просмотр карточек и файлов, плавное получение размеров, скачивание, загрузка, дозапись строк логов (для второго ESP) и удаление.

Страница интерфейса открывается на корне: `http://192.168.0.104/` — любой неизвестный путь тоже отдаёт её.

---
![enter image description here](hardware/esp32sd/esp32sd-B_Cu.svg)
![enter image description here](hardware/esp32sd/esp32sd-F_Cu.svg)
## Общие правила

### Параметр `card`
Все эндпоинты адресуются к карте памяти через параметр **`card`** — индекс SD-карты (список и порядок возвращает `/api/cards`):
- если параметр опущен — используется карта `0`;
- несуществующий индекс → `404 {"error":"card not found"}`;
- карта не готова (вышла, не смонтировалась) → `503 {"error":"storage not ready"}`.

### Пути
- Путь всегда **начинается с `/`** и передаётся **URL-кодированным** (пробел → `%20`, кириллица → `%D0%B0...`).
- `..` в путях запрещён → `400 {"error":"invalid path"}`.
- Имена файлов UTF-8/длинные имена FAT поддерживаются.

### Формат ошибок
Единообразный JSON c HTTP-кодом:

```
HTTP/1.1 404 Not Found
{"error":"file not found"}
```

| Код | Когда |
|-----|-------|
| 400 | неверные параметры (путь, пустое тело, попытка удалить корень) |
| 404 | карта/файл/папка не найдены |
| 500 | ошибка записи/удаления на стороне ESP32 |
| 503 | карта не готова (unmounted/not ready) |

### Особенности сервера (важно знать клиенту)
- Сервер **однопоточный и блокирующий**: пока идёт скачивание большого файла, остальные запросы ждут своей очереди. Логи (`/api/append` с частотой единицы мс на строку) можно слать постоянно, но не во время 10-мегабайтного скачивания.
- Соединения: ядро WebServer всегда отвечает с `Connection: close` — на каждый запрос создаётся новое TCP-соединение. Это нормально.
- POST-тело хранится в heap ESP32 как C-строка: **нулевые байты (`\0`) обрежут тело** — для текстовых логов это неактуально.

---

## 1. Список карт — `GET /api/cards`

```
GET /api/cards
```

Пример:
```cmd
curl.exe http://192.168.0.104/api/cards
```

```json
{
  "cards": [
    {"index":0,"name":"SD0","ready":true,"totalBytes":15518,"usedBytes":2048,"freeBytes":13470,"type":3},
    {"index":1,"name":"SD1","ready":false}
  ]
}
```

Поля: `name` — имя карты из `config.cpp`; `ready` — смонтирована ли; `type` — тип карточки (`3` = SDHC, `2` = SD, `1` = MMC). Байтовые поля есть только если `ready:true`. Все байты как `uint32` (карты >4 ТБ сюда не поместятся — у нас FAT32, не проблема).

---

## 2. Список файлов — `GET /api/files` (быстрый, без размеров)

```
GET /api/files?card=0&dir=/подпапка
```

| Параметр | Описание |
|----------|----------|
| `card` | индекс карты (по умолчанию `0`) |
| `dir` | **каталог для просмотра**. По умолчанию — `/`. Финальный слэш можно оставить, он срежется |

Отдаёт (только **один уровень**, без рекурсии) имена и тип объектов — **без размеров**. Размеры идут отдельно и плавно: `/api/sizes`.

Пример:
```cmd
curl.exe "http://192.168.0.104/api/files?card=0&dir=/"
```

```json
{
  "card": 0,
  "dir": "/",
  "children": [
    {"name":"logs","path":"/logs","dir":true},
    {"name":"test.txt","path":"/test.txt","dir":false},
    {"name":"IMG_0001.jpg","path":"/IMG_0001.jpg","dir":false}
  ]
}
```

Ответ **потоковый (chunked)** — даже при тысячах объектов ESP32 не забивает heap. Это базовый запрос интерфейса: список появляется почти мгновенно (один проход `readdir` без `stat` на каждый файл).

---

## 3. Плавное получение размеров — `GET /api/sizes`

```
GET /api/sizes?card=0&dir=/&start=0&count=24
```

| Параметр | Описание |
|----------|----------|
| `card`, `dir` | как в `/api/files` |
| `start` | **индекс входа в тот же список `children`** (порядок сохраняется), с которого начать (`0` в первый раз) |
| `count` | сколько обработать за раз (по умолчанию `24`, максимум `64`) |

Возвращает `count` записей: файлам вычислен размер, папкам посчитано количество объектов внутри. **`more` — индекс следующего захода** (`-1` — конец).

```json
{
  "items": [
    {"name":"logs","dir":true,"children":3},
    {"name":"test.txt","dir":false,"size":25},
    {"name":"глючный.tmp","dir":false,"err":true}
  ],
  "more": 24
}
```

`err:true` — объект исчез (удалён/частично недоступен) между запросами; клиент должен **сбросить своё "…"** на этой позиции.

Клиент продолжает запросы с `start = more`, пока `more != -1`. Пример цикла:

```bash
START=0
while :; do
  RESP=$(curl -s "http://192.168.0.104/api/sizes?card=0&dir=/&start=$START&count=24")
  echo "$RESP"   # разбираете JSON, обновляете UI
  START=$(echo "$RESP" | jq -r '.more')
  [ "$START" = "-1" ] && break
done
```

Веб-морда делает это сама, порциями по 24, обновляя ячейки по мере поступления.

---

## 4. Инфо о файле/папке — `GET /api/file/info`

```
GET /api/file/info?card=0&path=/logs/esp2.log
```

```json
{
  "card": 0, "cardName":"SD0",
  "path":"/logs/esp2.log", "name":"esp2.log",
  "dir": false, "size": 1024, "children": 0,
  "totalBytes": 15518, "usedBytes": 2048, "freeBytes": 13470
}
```

Для папки `"dir":true`, `"children"` — число объектов внутри (один обход readdir), `"size"` = 0.

---

## 5. Скачивание — `GET /download`

```
GET /download?card=0&path=/logs/esp2.log
```

Отдаёт файл как `application/octet-stream` c `Content-Disposition: attachment` (браузер предложит сохранить). Размер известен из `Content-Length` — прогресс считается по нему и загруженным байтам.

```cmd
curl.exe -o log.txt "http://192.168.0.104/download?card=0&path=/logs/esp2.log"
```

С прогрессом в браузере (XHR, как в UI):

```js
const xhr = new XMLHttpRequest();
xhr.open('GET', '/download?card=0&path=' + encodeURIComponent(path));
xhr.responseType = 'blob';
xhr.onprogress = ev => console.log((ev.loaded / size * 100).toFixed(0) + '%');
xhr.onload = () => {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(xhr.response);
  a.download = path.split('/').pop();
  a.click();
};
xhr.send();
```

---

## 6. Загрузка файла — `POST /upload`

```
POST /upload?card=0      (multipart/form-data, поле `file`)
```

Пример (curl сохранит файл на карте под его локальным именем):
```cmd
curl.exe -F "file=@C:\path\photo.jpg" "http://192.168.0.104/upload?card=0"
```

PowerShell 7:
```powershell
Invoke-RestMethod -F file=@C:\path\photo.jpg "http://192.168.0.104/upload?card=0"
```

Успех:
```json
{"ok":true,"message":"upload complete"}
```

**Особенность**: файл кладётся в **корень карты** (имя берётся из формы). Вложиться в подпапку можно хитростью — задать путь в `filename`:
```cmd
curl.exe -F "file=@C:\path\photo.jpg;filename=Photos/photo.jpg" "http://192.168.0.104/upload?card=0"
```
(папка `Photos` должна уже существовать).

---

## 7. Дозапись строки лога — `POST /api/append`  ← для второго ESP

```
POST /api/append?card=0&path=/logs/esp2.log      (Content-Type: text/plain)
```

Тело запроса = строка (или строки) лога, пишутся **как есть** в конец файла. Сервер гарантирует `\n` на конце, так что каждый запрос = завершённые строки. Родительские папки (`/logs`) создаются автоматически — клиенту не надо думать о структуре.

```cmd
curl.exe -X POST -H "Content-Type: text/plain" --data "boot OK, rssi=-58" "http://192.168.0.104/api/append?card=0&path=/logs/esp2.log"
```

Ответ:
```json
{"ok":true,"path":"/logs/esp2.log","bytes":17,"size":1024}
```
- `bytes` — сколько дописано;
- `size` — файл **после** дописи.

С **другого ESP32** (Arduino, HTTPClient):

```cpp
#include <HTTPClient.h>

void logToHost(const String& line) {
    HTTPClient http;
    http.begin("http://192.168.0.104/api/append?card=0&path=/logs/esp2.log");
    http.addHeader("Content-Type", "text/plain");
    int code = http.POST(line);        // тело = строка
    http.end();                        // (code == 200 → успех)
}
logToHost("runtime=" + String(millis()));
```

Важные детали:
- **Обязательно `Content-Type: text/plain`** (или любой не-формовый): иначе ядро разберёт тело как form и `plain` останется пустым → `400 empty body`.
- Тело пишется байт-в-байт: `\0` в тексте не отправлять.
- Несколько строк за раз — просто встаёт многострочное тело: `a\nb` → файл получит `a\nb\n`.
- Частота: одно дописывание ≈ единицы мс (open→append→close) — можно слать постоянно.
- В serial-мониторе виден каждый вызов: `[APPEND] /logs/esp2.log: +17 bytes (file size 1024)`.

---

## 8. Удаление — `GET /api/delete`

```
GET /api/delete?card=0&path=/test.txt
```

```json
{"ok":true,"path":"/test.txt","type":"file"}
```

- **Файл** — удаляется сразу.
- **Папка** — удаляется, **только если пустая**. Непустая → `500 {"error":"folder not empty (or delete failed)"}` (защита от сноса дерева).
- Корень `/` удалить нельзя → `400`.
- Папка/файл не существует → `404 not found`.

Пример удалить папку:
```cmd
curl.exe "http://192.168.0.104/api/delete?card=0&path=/tmp_folder"
```

В веб-морде у файлов и папок есть кнопка 🗑 — она откроет confirm и после успеха обновит список.

---

## 9. Логи самого ESP — `/logs/host.log` на карте из config

ESP32 сам пишет журнал всех принятых/отправленных HTTP-запросов и событий (монтирование карт, WiFi, append/delete/download/upload, ошибки). Строки однотипны:

```
[+12.3s] [API] /api/files card=0 dir=/ → 8 items · 41 ms
[+45.8s] [APPEND] card=0 /logs/esp2.log +17 bytes (size 1024)
[+51.2s] [DL] start card=0 /IMG_0001.jpg (60148 bytes)
```

- Метка `[+сек.s]` — время работы ESP (uptime). Файл-host выбирается **в `src/config.cpp`**:
  ```cpp
  const int    LOG_CARD_INDEX     = 0;          // ← карта, куда пишутся логи (индекс из /api/cards)
  const char*  LOG_DIR            = "/logs";
  const char*  LOG_FILE           = "host.log";
  const size_t LOG_MAX_FILE_BYTES = 256 * 1024; // ротация при превышении
  ```
- При превышении `LOG_MAX_FILE_BYTES` текущий `host.log` переезжает в `host.log.1` (хранится одна прошлая копия).
- Пока карта логов не готова — строки уходят в Serial и держатся в RAM-кольце (32 последние).
- `/api/file/info` и оболочка браузера (страница `/`) в журнал не пишутся — там шум.

---

## Сводка эндпоинтов

| Метод | Путь | Назначение |
|-------|------|-----------|
| GET | `/` | веб-интерфейс (и любой другой путь) |
| GET | `/api/cards` | список карт + их состояние |
| GET | `/api/files?card&dir` | быстрый список (один уровень, без размеров) |
| GET | `/api/sizes?card&dir&start&count` | плавный расчёт: размеры файлов / количество объектов у папок |
| GET | `/api/file/info?card&path` | сведения о файле/папке + статистика карты |
| GET | `/download?card&path` | скачать файл (octet-stream + attachment) |
| POST | `/upload?card` | загрузить файл (multipart, поле `file`) — в корень карты |
| POST | `/api/append?card&path` | дозаписать строку(-и) лога (text/plain) |
| GET | `/api/delete?card&path` | удалить файл / пустую папку |
| — | SD `/logs/host.log` | журнал запросов ESP (карта/лимиты — в `config.cpp`, `LOG_*`) |
