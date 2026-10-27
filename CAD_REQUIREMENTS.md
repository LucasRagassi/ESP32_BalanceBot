# Requisitos CAD da estrutura impressa

Este documento define entradas e verificações para o modelo CAD do BalanceBot V1; não pressupõe medidas de motor, roda ou driver que ainda não foram confirmadas.

## Envelope e arquitetura

- Altura nominal do robô: **300 mm**, a confirmar se inclui rodas, para-choques ou antena.
- Projetar chassi modular com placa base, suporte de ESP32, dois alojamentos de driver, IMU, bateria, conectores e tampa de serviço. Manter a placa IMU rigidamente ligada ao chassi, com eixos marcados e orientação documentada.
- Reservar o centro de gravidade baixo e próximo ao eixo das rodas; registrar massa e posição de bateria, motores, eletrônica e estrutura após montagem.
- Modelar os dois NEMA 17 pelos desenhos dimensionais dos modelos comprados (Creality 42-34 e 42HB34F08AB). Não assumir que os dois compartilham profundidade, eixo, padrão de montagem, comprimento de cabo ou torque.
- Usar interfaces/rasgos ajustáveis para distância entre eixos e montagem do motor até validar motor, roda, pneu e geometria de equilíbrio.
- Prever rodas coaxiais; alinhamento, paralelismo, cambagem, folga e concentricidade precisam ser medidos. Registrar diâmetro efetivo e razão mecânica no projeto.

## Fixação e serviço

- Usar padrões de furação obtidos de desenhos oficiais ou medição do componente em mãos. Incluir alívio de tolerância para impressão e acesso a parafusos, conectores, USB, reset e jumpers.
- Manter TMC2209 e dissipadores ventilados, sem contato com filamento/plástico suscetível a deformação térmica; incluir fluxo de ar e folga ao redor do driver.
- Separar cabos de motor/VM de IMU/I2C e antena Wi-Fi; canaletas com retenção contra tração e raio de curvatura compatível com fios.
- Criar suporte do E-stop acessível, protegido de acionamento acidental, mas alcançável durante ensaios, sem depender de acesso ao painel web.
- A tampa não pode obstruir dissipadores, antena, ventilação, bornes ou corte de emergência. Permitir inspeção de ENN/LED/diagnóstico.

## Material e manufatura

- Selecionar material pela temperatura dos drivers/motores, rigidez, resistência a impacto, vibração e temperatura ambiente; PLA não deve ser assumido apropriado junto a componentes quentes.
- Definir espessura de parede, nervuras, orientação de camadas e inserts/porcas cativas após protótipo. Evitar fixar motores apenas em paredes finas ou roscas impressas.
- Projetar proteções para fios e bordas, alívio de tensão, acesso de manutenção e pontos de fixação para contenção em bancada.
- Manter arquivos CAD paramétricos, lista de materiais e revisão/versionamento do desenho; exportar STL/3MF somente como derivados.

## Verificações de aceitação mecânica

1. Conferir no CAD e na montagem folgas, centro de gravidade e interferências em toda a rotação das rodas e faixa do chicote.
2. Verificar torque de parafusos, retenção de motores, rodas, bateria e PCB; aplicar teste manual de vibração/tração antes de energizar.
3. Medir dimensões, massa, distância entre eixos, raio da roda e posição do centro de gravidade da unidade montada.
4. Ensaiar dissipação dos TMC2209 com corrente conservadora e fonte limitada; interromper se temperatura exceder datasheet/material.
5. Testar E-stop sob energia de bancada e comprovar corte dos drivers independentemente do software.

## Pendências antes do CAD de fabricação

Desenhos oficiais dos motores, rodas e eixos; drivers e dissipadores reais; dimensões da PCB; bateria e suporte; definição do que conta nos 300 mm; massa máxima; torque/velocidade; seleção de filamento e processo de impressão.
