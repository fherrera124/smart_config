## Smartconfig ESP-IDF

#Description:
Programa de ejemplo cuya utilidad es conectarse a una red wifi cuando
sea posible. Para ello intenta de obtener desde el almacenamiento flash la
configuracion de una red almacenada, utilizando el sistema de archivos spiffs.
    - Si encuentra el archivo: Lo lee y trata de conectarse a la red especificada
    en el archivo un máximo de ESP_MAXIMUM_RETRY, si no logra conectarse
    iniciará smartconfig.
    - Si no encuentra el archivo: Iniciará smartconfig.

Smartconfig: si se encuentra en dicho modo, el dispositivo esperará
SMARTCONFIG_WAIT_TICKS para que una persona envie las credenciales usando la
aplicacion SmartTouch de espressif.
    - Si son proporcionadas, dichas credenciales se almacenaran en la memoria flash,
    reemplazando las ya existentes, de ser el caso. Luego intentará conectarse.
    - Si no son proporcionadas a tiempo (timeout), se reinicia el dispositivo y
    comienza nuevamente todo el proceso.
Siempre que la conexion se pierda, intentará unas ESP_MAXIMUM_RETRY veces para
volver a conectarse, caso fallido, iniciará smartconfig.