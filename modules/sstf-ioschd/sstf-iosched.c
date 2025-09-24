/*
 * Implementação do Agendador de E/S SSTF (Shortest Seek Time First)
 *
 * Desenvolvido para a versão 4.13.9 do Kernel Linux.
 * Este agendador prioriza a requisição que está fisicamente mais próxima
 * da posição atual da cabeça de leitura/escrita do disco.
 */

// Inclusão dos cabeçalhos necessários do kernel para manipulação de dispositivos de bloco,
// o sistema de elevador (agendador), BIOs (Block I/O), alocação de memória e inicialização de módulos.
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

/*
 * Estrutura de dados específica para o agendador SSTF.
 * Contém a fila de requisições e a posição atual da cabeça de leitura/escrita do disco.
 */
struct sstf_data {
	struct list_head queue;  // Fila que armazena as requisições pendentes.
	sector_t head_pos;       // Posição atual da cabeça do disco.
};

/*
 * Função chamada quando duas requisições adjacentes são unificadas.
 * A requisição 'next' é removida da lista, pois seu conteúdo foi mesclado à requisição 'rq'.
 */
static void sstf_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	// Remove a requisição 'next' da lista de espera do agendador.
	list_del_init(&next->queuelist);
}

/*
 * Função principal de despacho. Seleciona a próxima requisição a ser enviada ao disco.
 * A lógica é encontrar a requisição com o menor tempo de busca (menor distância do setor em relação à cabeça).
 */
static int sstf_dispatch(struct request_queue *q, int force)
{
	// Obtém os dados específicos do nosso agendador SSTF.
	struct sstf_data *nd = q->elevator->elevator_data;
	struct request *rq, *closest_rq = NULL;
	struct list_head *pos;
	// Inicializa com a maior distância possível para garantir que a primeira comparação funcione.
	sector_t closest_distance = ~((sector_t)0);

	// Se a fila de requisições estiver vazia, não há nada para fazer.
	if (list_empty(&nd->queue))
		return 0;

	// Itera sobre toda a fila para encontrar a requisição com o setor mais próximo da posição atual da cabeça.
	list_for_each(pos, &nd->queue) {
		rq = list_entry(pos, struct request, queuelist);
		// Calcula a distância absoluta (seek time) entre o setor da requisição e a posição da cabeça.
		sector_t distance = abs(blk_rq_pos(rq) - nd->head_pos);

		// Se a distância calculada for menor que a menor distância encontrada até agora,
		// atualiza a menor distância e armazena a requisição atual como a mais próxima.
		if (distance < closest_distance) {
			closest_distance = distance;
			closest_rq = rq;
		}
	}

	// Se uma requisição mais próxima foi encontrada, ela será despachada.
	if (closest_rq) {
		// Remove a requisição escolhida da fila do agendador.
		list_del_init(&closest_rq->queuelist);
		// Envia a requisição para a camada de despacho do kernel, que a colocará na fila de despacho do dispositivo.
		elv_dispatch_sort(q, closest_rq);
		// Atualiza a posição da cabeça para a posição da requisição que acabou de ser despachada.
		nd->head_pos = blk_rq_pos(closest_rq);
		printk(KERN_INFO "[SSTF] Despachando setor %llu (nova pos da cabeça: %llu)\n",
		       blk_rq_pos(closest_rq), nd->head_pos);
		return 1; // Retorna 1 para indicar que uma requisição foi despachada com sucesso.
	}

	return 0;
}

/*
 * Adiciona uma nova requisição à fila de espera.
 * Esta implementação insere a requisição de forma ordenada para otimizar a busca no despacho,
 * embora o SSTF clássico não exija uma fila ordenada, apenas uma busca completa no momento do despacho.
 * Esta abordagem pode ser ineficiente para inserções, mas simplifica o despacho.
 */
static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *nd = q->elevator->elevator_data;
	struct list_head *pos;
	struct request *iter_rq;

	// Procura a posição correta na fila para inserir a nova requisição, mantendo uma ordenação
	// baseada na proximidade com a posição atual da cabeça.
	list_for_each(pos, &nd->queue) {
		iter_rq = list_entry(pos, struct request, queuelist);
		// Compara a distância da requisição na lista com a distância da nova requisição.
		if (abs(blk_rq_pos(iter_rq) - nd->head_pos) > abs(blk_rq_pos(rq) - nd->head_pos)) {
			// Insere a nova requisição ('rq') antes da requisição iterada ('pos')
			// porque ela está mais perto da cabeça.
			list_add_tail(&rq->queuelist, pos);
			printk(KERN_INFO "[SSTF] Adicionado setor %llu (pos atual da cabeça: %llu)\n",
			       blk_rq_pos(rq), nd->head_pos);
			return;
		}
	}

	// Se o loop terminar sem encontrar uma posição, significa que a nova requisição é a mais distante
	// de todas, então ela é adicionada ao final da fila.
	list_add_tail(&rq->queuelist, &nd->queue);
	printk(KERN_INFO "[SSTF] Adicionado setor %llu ao final (pos atual da cabeça: %llu)\n",
	       blk_rq_pos(rq), nd->head_pos);
}

/*
 * Função de inicialização do agendador para uma determinada fila de requisições de um dispositivo de bloco.
 * Aloca a memória necessária para a estrutura de dados do SSTF.
 */
static int sstf_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct sstf_data *nd;
	struct elevator_queue *eq;

	// Aloca a estrutura base do elevador (agendador).
	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	// Aloca memória para nossa estrutura de dados personalizada (sstf_data).
	nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	if (!nd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}

	// Inicializa a cabeça da lista ligada que representará nossa fila.
	INIT_LIST_HEAD(&nd->queue);
	// A cabeça do disco é inicializada na posição 0 (setor inicial).
	nd->head_pos = 0;
	// Associa nossos dados personalizados à estrutura do elevador.
	eq->elevator_data = nd;

	// Adquire um spinlock para associar de forma segura nosso novo agendador à fila do dispositivo,
	// evitando condições de corrida.
	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);

	return 0;
}

/*
 * Função de finalização. Libera os recursos alocados pelo agendador quando ele é desativado.
 */
static void sstf_exit_queue(struct elevator_queue *e)
{
	struct sstf_data *nd = e->elevator_data;

	// Garante que a fila está vazia antes de ser destruída. Se não estiver, é um bug.
	BUG_ON(!list_empty(&nd->queue));
	// Libera a memória que foi alocada para a estrutura sstf_data.
	kfree(nd);
}

/*
 * Estrutura que define o nosso agendador para o kernel.
 * Ela mapeia as operações do agendador (adicionar, despachar, unir, inicializar e finalizar)
 * para as nossas funções correspondentes.
 */
static struct elevator_type elevator_sstf = {
	.ops.sq = {
		.elevator_merge_req_fn		= sstf_merged_requests,
		.elevator_dispatch_fn		= sstf_dispatch,
		.elevator_add_req_fn		= sstf_add_request,
		.elevator_init_fn		= sstf_init_queue,
		.elevator_exit_fn		= sstf_exit_queue,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

/*
 * Função de inicialização do módulo do kernel, chamada quando o módulo é carregado.
 */
static int __init sstf_init(void)
{
	// Registra nosso agendador no sistema de E/S do kernel para que ele possa ser utilizado.
	return elv_register(&elevator_sstf);
}

/*
 * Função de saída do módulo do kernel, chamada quando o módulo é descarregado.
 */
static void __exit sstf_exit(void)
{
	// Desregistra nosso agendador, removendo-o do sistema.
	elv_unregister(&elevator_sstf);
}

// Macros que definem as funções de inicialização e saída do módulo.
module_init(sstf_init);
module_exit(sstf_exit);

// Metadados do módulo.
MODULE_AUTHOR("Fernando Cabral, Luana Sosstisso e Fernanda Franceschini");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF IO scheduler");