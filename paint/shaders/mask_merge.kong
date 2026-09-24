
cbuffer constants {
	float opac;
	int blending;
};

sampler sampler_linear;

tex2d tex0;

tex2d texa;

struct vert_in {
	float2 pos;
};

struct vert_out {
	float4 pos;
	float2 tex;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.tex = input.pos.xy * 0.5 + 0.5;
	output.tex.y = 1.0 - output.tex.y;
	output.pos = float4(input.pos.xy, 0.0, 1.0);
	return output;
}

float4 frag(vert_out input) {
	float col0 = sample_lod(tex0, sampler_linear, input.tex, 0.0).r;
	float cola = sample_lod(texa, sampler_linear, input.tex, 0.0).r;
	float str = constants.opac;
	float out_color = 0.0;

	if (constants.blending == 0) { // Mix
		out_color = lerp(cola, col0, str);
	}
	/*else*/ if (constants.blending == 1) { // Darken
		out_color = lerp(cola, min(cola, col0), str);
	}
	/*else*/ if (constants.blending == 2) { // Multiply
		out_color = lerp(cola, cola * col0, str);
	}
	/*else*/ if (constants.blending == 3) { // Burn
		out_color = lerp(cola, 1.0 - (1.0 - cola) / col0, str);
	}
	/*else*/ if (constants.blending == 4) { // Lighten
		out_color = max(cola, col0 * str);
	}
	/*else*/ if (constants.blending == 5) { // Screen
		out_color = (1.0 - ((1.0 - str) + str * (1.0 - col0)) * (1.0 - cola));
	}
	/*else*/ if (constants.blending == 6) { // Dodge
		out_color = lerp(cola, cola / (1.0 - col0), str);
	}
	/*else*/ if (constants.blending == 7) { // Add
		out_color = lerp(cola, cola + col0, str);
	}
	/*else*/ if (constants.blending == 8) { // Overlay
		// out_color = lerp(cola, cola < 0.5 ? 2.0 * cola * col0 : 1.0 - 2.0 * (1.0 - cola) * (1.0 - col0), str);

		////
		//if (cola < 0.5) {
			out_color = lerp(cola, 2.0 * cola * col0, str);
		//}
		//else {
		//	out_color = lerp(cola, 1.0 - 2.0 * (1.0 - cola) * (1.0 - col0), str);
		//}
		////
	}
	/*else*/ if (constants.blending == 9) { // Soft Light
		out_color = ((1.0 - str) * cola + str * ((1.0 - cola) * col0 * cola + cola * (1.0 - (1.0 - col0) * (1.0 - cola))));
	}
	/*else*/ if (constants.blending == 10) { // Linear Light
		out_color = (cola + str * (2.0 * (col0 - 0.5)));
	}
	/*else*/ if (constants.blending == 11) { // Difference
		out_color = lerp(cola, abs(cola - col0), str);
	}
	/*else*/ if (constants.blending == 12) { // Subtract
		out_color = lerp(cola, cola - col0, str);
	}
	/*else*/ if (constants.blending == 13) { // Divide
		out_color = (1.0 - str) * cola + str * cola / col0;
	}
	/*else*/ if (constants.blending == 14) { // Hue, Saturation, Color, Value
		out_color = lerp(cola, col0, str);
	}

	return float4(out_color, out_color, out_color, 1.0);
}
