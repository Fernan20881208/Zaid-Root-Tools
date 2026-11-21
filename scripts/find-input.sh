#!/system/bin/sh
# Informacion de los dispositivos; no escucha toques ni registra pulsaciones.
echo "=== Dispositivos input del kernel ==="
su -c '/system/bin/getevent -il'
echo ""
echo "Busca eventN (botones de volumen o mando) y pon N en ajustes de Geode."
echo "Para confirmar un boton concreto, desde Termux y conscientemente:"
echo "  su -c /system/bin/getevent -lt /dev/input/eventN"
echo "Deten con Ctrl+C. No selecciones el touchscreen ni el teclado completo."
