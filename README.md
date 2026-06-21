Este projeto é objeto avaliativo do segundo TDE da disciplina de Fundamentos de Arquitetura de Computadores, e visa simular uma memória cache com parâmetros configuráveis.
Os parâmetros devem ser passados como argumentos na função main, sendo eles:
Política de escrita: 0 para write-through e 1 para write-back
Tamanho do bloco: tamanho em bytes (potência de 2)
Número total de blocos: deve ser potência de 2
Associatividade: deve ser potência de 2 (mínimo 1 e máximo número total de blocos)
Tempo de acesso quando dá hit: tempo em nanossegundos
Política de substituição: 0 para LRU e 1 para Random
Tempo de leitura/escrita da memória principal: tempo em nanossegundos