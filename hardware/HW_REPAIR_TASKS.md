# 🛠️ Tareas de Reparación de Hardware (Brújula LIS3MDL)

Para que el microcontrolador Seeed Studio pueda comunicarse con la brújula en tu PCB customizada, debes realizar la siguiente modificación física en la placa. Es 100% seguro y funcional debido al pequeño tamaño de tu reloj.

---

## 📋 Lista de Pasos a Realizar

### [ ] 1. Localizar las resistencias de 4.7kΩ en serie
- Encuentra en tu PCB las dos resistencias de 4.7kΩ (formato 0402) situadas en las líneas de comunicación:
  - La resistencia en la línea **D4 (SDA)**.
  - La resistencia en la línea **D5 (SCL)**.

### [ ] 2. Desoldar las resistencias
- Con la ayuda de tu soldador y unas pinzas finas, retira con cuidado ambas resistencias 0402 de sus pads correspondientes.

### [ ] 3. Crear los puentes de soldadura (0Ω)
- Coloca una pequeña gota de estaño en la punta del soldador.
- Pásala suavemente sobre los dos pads de la resistencia de la línea **D4** de modo que el estaño se fusione y conecte ambos pads directamente (creando un puente de estaño).
- Repite el mismo proceso sobre los dos pads de la resistencia de la línea **D5**.
- *Nota: Con esto eliminamos la caída de voltaje y dejamos el bus conectado de forma directa sin resistencias limitadoras.*

### [ ] 4. Flashear y comprobar
- Enciende el reloj y conéctalo al puerto USB.
- Sube el firmware actual (versión v1.4) desde el IDE de Arduino.
- Abre el **Monitor Serie (115200 baudios)**.
- Deberías ver que el firmware arranca de inmediato y muestra el mensaje de éxito:
  `[COMPASS] ✓ LIS3MDL found on Wire (D4/D5) at 0x1E`
- ¡Listo! La brújula ya estará operativa y se comunicará utilizando las resistencias de elevación (pull-ups) internas del Seeed Studio de forma fiable.
