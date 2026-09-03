/*
	Copyright (c) 2023, GigaDevice Semiconductor Inc.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#ifndef NN_MODEL_CONFIGURE
#define NN_MODEL_CONFIGURE
#pragma once

#include "gd_nn_tensor.h"
#include "gd_nn_layer.h"
#include "gd_nn_support.h"
#include "gd_nn_interface.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define INPUT_SIZE				 1980
#define INPUT_TYPE_SIZE			 4
/* model output node number */
#define MODEL_OUTPUT_COUNT		 1

#define OUTPUT_SIZE				 8
#define OUTPUT_TYPE_SIZE		 4
/* all of model output node size occupy */
#define MODEL_OUTPUT_SIZE_ALL				 32 /*bytes*/

/* if user do not need benchmark, #undef BENCHMAR */
#define BENCHMARK   
#define BENCHMARK_PAYLOAD_HEADER	 16

/* 0: float;  1: int8 or uint8 */
#define OUT_TYPE			 0

/* input and output data */
#if defined (BENCHMARK)     /* because of benchmark in/out arr need address and size both 32 align */ 
#define BENCHMARK_INPUT_BUFFER_SIZE      ((INPUT_SIZE + BENCHMARK_PAYLOAD_HEADER) + (32 - (INPUT_SIZE + BENCHMARK_PAYLOAD_HEADER) % 32)) 
extern float* input_data;
#else          
extern float* input_data;
#endif          

/* model's peak buffer£¬if user create more model beyond one, please max this macr */
#define STATIC_BUFFER_PEAK_SIZE        13344
/* model's layer malloc size, if user create more model beyond one, please sum this macro */
#define AI_LAYER_MEM_SIZE              1180
/*other buf size */
#define CMSIS_NN_CTX_BUFFER_SIZE       1
#define MAX_OUT_CH_SIZE                1
#define MAX_KERNAL_SIZE                3
#define MAX_CONV_SBUF_SIZE             96
#define SCRATCH_BUFFER_SIZE            16
#define LSTM_BUFFER_SIZE               1
#define LSTM_NUMBERS                   1
#define OP_SUM_BUFFER_SIZE             1

/* static buffer pool declare */
extern nn_uint8 static_buffer_peak[];
/* model output node(OUT0, OUT1, OUT2...) offset by start addr of static_buffer_peak */
extern uint32_t out_node_arr_offset[];
/* model output node(OUT0, OUT1, OUT2...) size(bytes) */
extern uint32_t out_node_arr_size[];
/* operators buffer declare */
extern nn_uint8 ctx_buffer[];
extern nn_float conv_buf[];
extern uint32_t kernal_buf[];
extern int32_t bias_data_buffer[];
extern int32_t scratch_buffer[];
extern nn_uint8 lstm_buffer[];
extern lstm_status_info* lstm_status_info_arr[];
extern int32_t op_sum_buffer[];

/* callback function declare */
extern operator_callback func_cb_arr[];

/* report declare */
extern char* operator_name[];
extern char* model_name;
extern char* platform_name;

/* optimize level */
extern uint8_t optimize_level;
/* whether using sdram or not */
extern uint8_t using_sdram;
/* get lstm status info */
extern uint32_t lstm_status_index;
extern uint32_t lstm_numbers;

/* need lstm cell and out state or not */ 
extern uint8_t undirectional_lstm_cell_and_out_state_need_clear_every_invoke;
extern uint8_t bidirectional_lstm_cell_and_out_state_need_clear_every_invoke;

/*  debug every layer's output in serial*/
 extern uint8_t debug_print_result;

/* param array declare */
#define MODEL_ARR_MAPPING_IN_FLASH  0

/*For balanced optimize, Model data params which beyond 32k Cache, user put them in RAM by set 0 to get better speed */ 
#define MODEL_DATA_MAPPING_IN_FLASH  0

#if MODEL_ARR_MAPPING_IN_FLASH == 1
	#define MAPPING const
#else
	#define MAPPING
#endif

extern MAPPING uint8_t model_paras_arr[];

#if MODEL_DATA_MAPPING_IN_FLASH == 1
	#define MAPPING_DATA const
#else
	#define MAPPING_DATA
#endif

extern MAPPING_DATA uint8_t model_paras_data[];

#ifdef __cplusplus
}
#endif

#endif
