#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <vector>

typedef struct cacheConfig
{
    unsigned char write_policy; // 0 eh write through e 1 eh write back
    unsigned int line_size;
    unsigned int line_count;
    unsigned int lines_per_set; // min 1 e max = line_count
    unsigned char hit_time = 4;
    unsigned char replacement_policy; // 0 eh LRU e 1 eh random
} cacheConfig;

typedef struct mainMemory
{
    unsigned short read_time = 60;
    unsigned short write_time = read_time;
} mainMemory;

typedef struct
{
    bool valid; // Indica se a linha da cache tem um valor valido ou se ainda nao foi usada
    int tag;
    bool dirty;
    int lru;
} CacheLine;

int main(int argc, char *argv[])
{
    if (argc < 8)
    {
        printf("Erro. Parametros de configuracao insuficientes");
        return 1;
    }

    // Inicializa a struct de configuração da cache e da memoria principal
    cacheConfig cache_cfg;
    cache_cfg.write_policy = (unsigned char)atoi(argv[1]);
    cache_cfg.line_size = (unsigned int)atoi(argv[2]);
    cache_cfg.line_count = (unsigned int)atoi(argv[3]);
    cache_cfg.lines_per_set = (unsigned int)atoi(argv[4]);
    cache_cfg.hit_time = (unsigned char)atoi(argv[5]);
    cache_cfg.replacement_policy = (unsigned char)atoi(argv[6]);

    mainMemory memory_cfg;
    memory_cfg.read_time = (unsigned short)atoi(argv[7]);
    memory_cfg.write_time = memory_cfg.read_time;

    unsigned int sets = cache_cfg.line_count / cache_cfg.lines_per_set;

    // vetor onde cada posicao representa um conjunto com uma estrutura CacheLine dentro
    std::vector<CacheLine> cache(sets * cache_cfg.lines_per_set, {0, 0, 0, 0});

    FILE *addresses = fopen("teste.cache", "r");
    if (!addresses)
    {
        printf("Erro ao abrir arquivo,\n");
        return 1;
    }

    // Inicializa seed para o random se precisar
    srand(time(NULL));

    // Variaveis para contagem de operacoes, acertos, erros e calculo de taxas
    unsigned int op_write_cnt = 0, op_read_cnt = 0;
    unsigned int mem_write_cnt = 0, mem_read_cnt = 0;
    unsigned int write_miss_cnt = 0, read_miss_cnt = 0, total_miss_cnt = 0;
    unsigned int write_hit_cnt = 0, read_hit_cnt = 0, total_hit_cnt = 0;
    float write_miss_rate = 0, read_miss_rate = 0, total_miss_rate = 0;
    float write_hit_rate = 0, read_hit_rate = 0, total_hit_rate = 0;
    unsigned int global_cnt = 0; // contador global para o LRU

    // calcula o numero de bits para o deslocamento da palavra
    int word_bits = 0;
    unsigned int tmp = cache_cfg.line_size;
    while (tmp > 1)
    {
        word_bits++;
        tmp >>= 1;
    }

    // calcula o numero de bits para o deslocamento do conjunto
    int set_bits = 0;
    tmp = sets;
    while (tmp > 1)
    {
        set_bits++;
        tmp >>= 1;
    }

    // Tamanho um pouco maior pra garantir que vai ler a linha toda
    char line[16];

    // Le o arquivo linha por linha, processando cada endereco e operacao
    while (fgets(line, sizeof(line), addresses))
    {
        unsigned int address;
        char op;

        // Tenta ler o endereco e a operacao da linha
        if (sscanf(line, "%x %c", &address, &op) != 2)
        {
            printf("Erro ao ler linha do arquivo: %s\n", line);
            continue;
        }

        global_cnt++; // Incrementa o contador global para o LRU

        // Calcula a palavra, o conjunto e a tag
        unsigned int word = address & ((1 << word_bits) - 1);
        unsigned int set = (address >> word_bits) & ((1 << set_bits) - 1);
        unsigned int tag = address >> (word_bits + set_bits);

        unsigned int set_start_index = set * cache_cfg.lines_per_set;
        bool hit = false;
        long int hit_index = -1; // Deixei long pra nao ter risco de estourar o int pelo current index ser unsigned

        // Verifica se o endereco ja esta na cache, iterando pelas linhas do conjunto
        for (unsigned int i = 0; i < cache_cfg.lines_per_set; i++)
        {
            unsigned int current_index = set_start_index + i;
            if (cache[current_index].valid && cache[current_index].tag == tag)
            {
                hit = true;
                hit_index = current_index;
                break;
            }
        }

        // Se der hit...
        if (hit)
        {
            // Se for leitura so conta
            if (op == 'R')
            {
                op_read_cnt++;
                read_hit_cnt++;
            }
            // Se for escrita tem que verificar a politica de escrita pra ver se conta a escrita na memoria ou so na cache
            else if (op == 'W')
            {
                op_write_cnt++;
                write_hit_cnt++;
                if (cache_cfg.write_policy == 0) // Se for write_through
                    mem_write_cnt++;             // So conta a escrita na memoria
                else
                    cache[hit_index].dirty = true; // Se for write back seta o dirty bit
            }

            // Atualiza o LRU da linha que deu hit
            cache[hit_index].lru = global_cnt; // Atualiza o contador global para o LRU
        }
        // Se der miss...
        else
        {
            bool allocate_line = true; // Indica se precisa alocar uma linha nova na cache

            // Se for leitura conta o miss e a leitura na memoria
            if (op == 'R')
            {
                op_read_cnt++;
                read_miss_cnt++;
                mem_read_cnt++;
            }
            // Se for escrita tem que verificar a politica de escrita pra ver se conta a escrita
            // na memoria ou so na cache, e se precisa alocar uma linha nova na cache
            else if (op == 'W')
            {
                op_write_cnt++;
                write_miss_cnt++;
                if (cache_cfg.write_policy == 0) // Se for write through
                {
                    mem_write_cnt++;       // vai salvar na memoria toda vez que der miss ao escrever na cache
                    allocate_line = false; // Nao precisa alocar na cache se for write through, escreve direto na memoria
                }
                else
                {
                    mem_read_cnt++; // Se for write back tem que ler a linha da memoria pra depois modificar ela e salvar na cache,
                                    // entao conta a leitura da memoria nesse caso
                }
            }

            // Se precisa alocar uma linha nova na cache, tem que verificar se tem alguma linha livre no conjunto
            // ou se precisa aplicar a politica de substituicao
            if (allocate_line)
            {
                int replace_index = -1;

                // Tenta achar uma linha livre
                for (unsigned int i = 0; i < cache_cfg.lines_per_set; i++)
                {
                    unsigned int current_index = set_start_index + i;
                    if (!cache[current_index].valid)
                    {
                        replace_index = current_index;
                        break;
                    }
                }

                // Se nao achou nenhuma linha livre aplica substituicao
                if (replace_index == -1)
                {
                    if (cache_cfg.replacement_policy == 0) // Se for LRU
                    {
                        int min_lru = cache[set_start_index].lru;
                        replace_index = set_start_index;
                        // Itera pra ver qual linha tem o menor lru (consequentemente vai ser a menos usada)
                        for (unsigned int i = 1; i < cache_cfg.lines_per_set; i++)
                        {
                            unsigned int current_index = set_start_index + i;
                            if (cache[current_index].lru < min_lru)
                            {
                                min_lru = cache[current_index].lru;
                                replace_index = current_index;
                            }
                        }
                    }
                    else // Escolhe linha aleatoria dentro do conjunto
                    {
                        replace_index = set_start_index + (rand() % cache_cfg.lines_per_set);
                    }

                    // Se for write back e a linha tinha mudado escreve na memoria
                    if (cache_cfg.write_policy == 1 && cache[replace_index].dirty)
                    {
                        mem_write_cnt++;
                    }
                }

                // Atualiza a linha escolhida com os novos dados
                cache[replace_index].valid = true;
                cache[replace_index].tag = tag;
                cache[replace_index].dirty = (op == 'W' && cache_cfg.write_policy == 1);
                cache[replace_index].lru = global_cnt;
            }
        }
    }

    // Fecha o arquivo depois de processar tudo
    fclose(addresses);

    // Verifica se precisa gravar a cache de volta na memoria (se for write back e tiver linhas sujas)
    if (cache_cfg.write_policy == 1)
    {
        for (unsigned int i = 0; i < cache.size(); i++)
        {
            if (cache[i].valid && cache[i].dirty)
            {
                mem_write_cnt++;
                cache[i].dirty = false; // Depois de escrever na memoria, a linha nao esta mais suja
            }
        }
    }

    /* Calcula as taxas de acerto */
    unsigned int total_accesses = op_read_cnt + op_write_cnt;
    read_hit_rate = op_read_cnt ? ((float)read_hit_cnt / op_read_cnt) * 100 : 0;
    write_hit_rate = op_write_cnt ? ((float)write_hit_cnt / op_write_cnt) * 100 : 0;
    total_hit_rate = total_accesses ? ((float)(read_hit_cnt + write_hit_cnt) / total_accesses) * 100 : 0;

    unsigned long long total_cycles = (total_accesses * cache_cfg.hit_time) +
                                      (mem_read_cnt * memory_cfg.read_time) +
                                      (mem_write_cnt * memory_cfg.write_time);

    float cache_avg_time = total_accesses ? (float)total_cycles / total_accesses : 0;

    /* ------------  Area das saidas  -------------*/
    printf("--------------------------------------------------------------------------------------\n");
    printf("Parametros de configuracao:\n");
    printf("Politica de escrita: %s\n", cache_cfg.write_policy == 0 ? "Write-through" : "Write-back");
    printf("Tamanho da linha: %u bytes\n", cache_cfg.line_size);
    printf("Numero de linhas: %u\n", cache_cfg.line_count);
    printf("Linhas por conjunto: %u\n", cache_cfg.lines_per_set);
    printf("Tempo de acerto: %u ciclos\n", cache_cfg.hit_time);
    printf("Politica de substituicao: %s\n", cache_cfg.replacement_policy == 0 ? "LRU" : "Random");
    printf("Tempo de leitura na memoria principal: %uns\n", memory_cfg.read_time);
    printf("Tempo de escrita na memoria principal: %uns\n", memory_cfg.write_time);
    printf("--------------------------------------------------------------------------------------\n");
    printf("Numero de enderecos de escrita: %d\n", op_write_cnt);
    printf("Numero de enderecos de leitura: %d\n", op_read_cnt);
    printf("Total de enderecos processados: %d\n", total_accesses);
    printf("--------------------------------------------------------------------------------------\n");
    printf("Total de escritas na memoria principal: %d\n", mem_write_cnt);
    printf("Total de leitura na memoria principal: %d\n", mem_read_cnt);
    printf("--------------------------------------------------------------------------------------\n");
    printf("Taxa de acerto (hit rate) por leitura: %.4f%%\n", read_hit_rate);
    printf("Taxa de acerto (hit rate) por escrita: %.4f%%\n", write_hit_rate);
    printf("Taxa de acerto (hit rate) global: %.4f%%\n", total_hit_rate);
    printf("--------------------------------------------------------------------------------------\n");
    printf("Tempo medio de acesso da cache: %.4fns\n", cache_avg_time);
    return 0;
}
