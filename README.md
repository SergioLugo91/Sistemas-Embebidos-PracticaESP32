Práctica con el ESP32

Autor: Sergio Lugo Couros

Sensor que detecte la concentración de gases inflamables, página web creada por la ESP que muestre las lecturas del sensor y el aviso cuando se dispare la alarma. Activar también un zumbador que cambie la frecuencia del pitido según la concentración de gas.

EEPROM --> guardar valor límite para la concentración de gas, que se pueda modificar en la página web.

Modo de sueño ligero --> cuando pasan 10 segundos sin tener cliente en la página web la ESP entra en modo de sueño ligero y se despierta cada 5 segundos para hacer una medición y comprobar si hay un nuevo cliente.

WIFI --> crear una página web donde se envíen los datos de lectura del sensor, se pueda observar cuando se dispara la alarma y cambiar el valor límite de concentración.

Interrupción --> usar uno de los sensores capacitivos para encender la placa y mantenerla enviando datos a la página web hasta que se vuelva a activar el sensor. 
