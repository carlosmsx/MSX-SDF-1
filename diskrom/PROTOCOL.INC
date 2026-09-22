;Protocolo entre la ROM y el firmware del ATmega. Lo incluyen las dos paginas
;de la ROM: DSKDRV.MAC (pagina 1) y PAGE2.MAC (pagina 2). Todo lo de aca tiene
;que coincidir con defs.h del firmware.
;
;Comandos agrupados por funcion, un nibble alto por grupo:
;  09xH dispositivos de BASIC, 0AxH reloj del kernel, 0DxH diagnostico,
;  0ExH SD e imagenes DSK, 0FxH driver de disco de MSX-DOS

;Version del protocolo con el firmware: tiene que coincidir con
;PROTOCOL_VERSION en defs.h. La compara CALL SDFTEST.
PROTOCOL	EQU		6

;09xH: tubo de dispositivos de BASIC (OPEN "RTC:"...). La ROM no sabe que
;dispositivos hay: pregunta el nombre y despues pasa bytes. Un dispositivo
;nuevo es firmware, no ROM.
CMD_DEVNAME	EQU		090H	;manda PROCNM ASCIIZ, contesta ID 0-3 o 0FFH
CMD_DEVOPEN	EQU		091H	;manda ID, modo, largo y el texto tras los ":"
CMD_DEVCLOSE	EQU	092H
CMD_DEVIN	EQU		093H	;un byte por lectura, 0 = fin de datos
CMD_DEVOUT	EQU		094H	;bytes; el CR ejecuta la linea y deja el resultado
CMD_DEVEOF	EQU		095H	;0FFH fin o dato no confiable, 0 si hay datos
;0AxH: reloj del kernel. CHKCLK/$GETTI/$SETDA/$SETTI parchados con JP a
;MYCHKCLK/MYGETTI/MYSETDA/MYSETTI (DSKDRV.MAC), que hablan con el firmware
;por el protocolo normal en vez de puertos crudos (camino A, ver
;reloj-cuatro-caminos.md: el camino B fallaba por timing en el bucle de
;deteccion del RP-5C01, el mas apretado de toda la ROM).
CMD_RTCCHK	EQU		0A0H	;sin datos, contesta 1 byte (0FFH=hay reloj, 0=no)
CMD_RTCGET	EQU		0A1H	;contesta 7 bytes: anio-1980,mes,dia,hora,min,seg,diasem
CMD_RTCSETDA	EQU	0A2H	;recibe 3 bytes: anio-1980,mes,dia
CMD_RTCSETTI	EQU	0A3H	;recibe 3 bytes: hora,min,seg
;0DxH: diagnostico
CMD_DEBUG	EQU		0D0H
CMD_SDFTEST	EQU		0D1H
CMD_SDFDEBUG	EQU	0D2H	;el firmware manda texto: lo imprime CALL SDFDEBUG
;0ExH: SD e imagenes DSK
CMD_SENDSTR	EQU		0E0H	;de antes de agrupar; hoy no lo usa nadie
CMD_SDFMOUNT	EQU		0E1H
CMD_SDFFILES	EQU		0E2H
CMD_SDFUMOUNT	EQU		0E3H
CMD_SDFNEW	EQU		0E4H
;0FxH: driver de disco de MSX-DOS
CMD_WRITE	EQU		0F0H
CMD_READ	EQU		0F1H
CMD_INIHRD	EQU		0F2H
CMD_INIENV	EQU		0F3H
CMD_DRIVES	EQU		0F4H
CMD_DSKCHG	EQU		0F5H
CMD_CHOICE	EQU		0F6H
CMD_DSKFMT	EQU		0F7H
CMD_OEMSTAT	EQU		0F8H
CMD_MTOFF	EQU		0F9H
CMD_GETDPB	EQU		0FAH

;Media de los dos formatos: los manda CMD_SDFNEW como tamaño y los contesta
;CMD_DSKFMT segun el largo de la imagen (0 si no hay imagen)
MEDIA_720K	EQU		0F9H
MEDIA_360K	EQU		0F8H

;Mientras el firmware crea la imagen de CMD_SDFNEW, contesta esto en vez del
;resultado. No choca con los errores de BASIC ni con el bus flotando (0FFH).
SDFNEW_BUSY	EQU		0FEH

;Drive interno con la imagen recien creada por CMD_SDFNEW: el DOS no lo ve,
;solo sirve para que la ROM le escriba los sectores de sistema.
NEW_DRIVE	EQU		2

;Mientras el firmware ejecuta una linea de CMD_DEVOUT contesta esto en vez del
;resultado, igual que SDFNEW_BUSY.
DEV_BUSY	EQU		0FEH

;Error de BASIC que devuelve la ROM si el firmware contesta cualquier cosa con
;el bit 7 puesto (bus flotando en 0FFH, firmware viejo).
ERR_DEVICE_IO	EQU	19

;----I/O Ports----
PORT_CMD	EQU		1
PORT_DATA	EQU		0
