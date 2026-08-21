
#include "../global.h"

void neural_node_models_init() {
	neural_node_models = any_array_create_from_raw(
	    (void *[]){
	        ALLOC_INIT(neural_node_model_t, {.name   = "FLUX 2 klein",
	                                            .memory = "4GB",
	                                            .size   = "8.7GB",
	                                            .nodes  = "Text to Image, Edit Image",
	                                            .urls   = any_array_create_from_raw(
                                                    (void *[]){
                                                        "https://huggingface.co/armory3d/FLUX.2-klein-4B-GGUF/resolve/main/flux-2-klein-4b-Q8_0.gguf",
                                                        "https://huggingface.co/armory3d/FLUX.2-klein-4B-GGUF/resolve/main/Qwen3-4B-Q8_0.gguf",
                                                        "https://huggingface.co/armory3d/FLUX.2-klein-4B-GGUF/resolve/main/vae.safetensors",
                                                        "https://huggingface.co/armory3d/FLUX.2-klein-4B-GGUF/resolve/main/tokenizer.json",
                                                    },
                                                    4),
	                                            .web     = "https://huggingface.co/armory3d/FLUX.2-klein-4B-GGUF",
	                                            .license = "apache-2.0"}),

	        ALLOC_INIT(neural_node_model_t, {.name   = "DA3MONO",
	                                            .memory = "6GB",
	                                            .size   = "1.4GB",
	                                            .nodes  = "Image to PBR",
	                                            .urls   = any_array_create_from_raw(
                                                    (void *[]){
                                                        "https://huggingface.co/armory3d/DA3MONO-LARGE/resolve/main/da3-mono-large.safetensors",
                                                    },
                                                    1),
	                                            .web     = "https://huggingface.co/armory3d/DA3MONO-LARGE",
	                                            .license = "apache-2.0"}),

	        ALLOC_INIT(neural_node_model_t, {.name   = "Real-ESRGAN",
	                                            .memory = "1GB",
	                                            .size   = "0.07GB",
	                                            .nodes  = "Upscale Image",
	                                            .urls   = any_array_create_from_raw(
                                                    (void *[]){
                                                        "https://huggingface.co/armory3d/Real-ESRGAN/resolve/main/RealESRGAN_x4plus.safetensors",
                                                    },
                                                    1),
	                                            .web     = "https://huggingface.co/armory3d/Real-ESRGAN",
	                                            .license = "bsd-3-clause"}),

	        ALLOC_INIT(neural_node_model_t, {.name   = "Hunyuan3D",
	                                            .memory = "12GB",
	                                            .size   = "12.6GB",
	                                            .nodes  = "Image to 3D Mesh",
	                                            .urls   = any_array_create_from_raw(
                                                    (void *[]){
                                                        "https://huggingface.co/armory3d/hunyuan3d21_portable/resolve/main/Hunyuan3D_win64.tar",
                                                    },
                                                    1),
	                                            .web     = "https://huggingface.co/armory3d/hunyuan3d21_portable",
	                                            .license = "hunyuan3d"}),

	        ALLOC_INIT(neural_node_model_t, {.name   = "Qwen",
	                                            .memory = "20GB",
	                                            .size   = "15.3GB",
	                                            .nodes  = "Text to Text, Console",
	                                            .urls   = any_array_create_from_raw(
                                                    (void *[]){
                                                        "https://huggingface.co/unsloth/Qwen3.8-27B-GGUF/resolve/main/Qwen3.8-27B-UD-Q4_K_M.gguf",
                                                    },
                                                    1),
	                                            .web     = "https://huggingface.co/unsloth/Qwen3.8-27B-GGUF",
	                                            .license = "apache-2.0"}),
	    },
	    5);
}
