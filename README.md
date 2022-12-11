# Smartconfig ESP-IDF

## Description:
Programa de ejemplo cuya utilidad es conectarse a una red wifi cuando
sea posible. Para ello intenta de obtener desde el almacenamiento flash la
configuracion de una red almacenada, utilizando el sistema de archivos spiffs.
- Si encuentra el archivo: Lo lee y trata de conectarse a la red especificada
  en el archivo un número máximo especificado de intentos. Si no lo logra
  iniciará smartconfig.
- Si no encuentra el archivo: Iniciará smartconfig.

### Smartconfig
si se encuentra en dicho modo, el dispositivo esperará un
tiempo determinado por credenciales desde la aplicacion SmartTouch de espressif.

- Si son proporcionadas: intentará conectarse.
  - Si logra conectarse: dichas credenciales se almacenarán en la memoria
    flash, reemplazando las anteriores, en caso de existir.
  - Si no logra conectarse: luego de alcanzar el número máximo de intentos,
    reiniciará.

- Si no son proporcionadas (timeout): se reinicia el dispositivo y comienza
  nuevamente todo el proceso.

Nota: Siempre que la conexion se pierda, intentará volver a conectarse un máximo
especificado de intentos. Si no lo logra iniciará smartconfig.
