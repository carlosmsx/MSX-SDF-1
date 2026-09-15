;Protocolo entre la ROM y el firmware del ATmega. Lo incluyen las dos paginas
;de la ROM: DSKDRV.MAC (pagina 1) y PAGE2.MAC (pagina 2). Todo lo de aca tiene
;que coincidir con defs.h del firmware.
;
;Comandos agrupados por funcion, un nibble alto por grupo:
;  0DxH diagnostico, 0ExH SD e imagenes DSK, 0FxH driver de disco de MSX-DOS

;Version del protocolo con el firmware: tiene que coincidir con
;PROTOCOL_VERSION en defs.h. La compara CALL SDFTEST.
PROTOCOL	EQU		3

;0DxH: diagnostico
CMD_DEBUG	EQU		0D0H
CMD_SDFTEST	EQU		0D1H
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

;----I/O Ports----
PORT_CMD	EQU		1
PORT_DATA	EQU		0
