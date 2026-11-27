
#ifdef FORMAT_RGBA32
tex2d<rgba32> target;
#endif
#ifdef FORMAT_RGBA64
tex2d<rgba64> target;
#endif
#ifdef FORMAT_RGBA128
tex2d<rgba128> target;
#endif

tex2d texindex; // Dilation index map
tex2d source;   // Temp texture

#[compute, threads(8, 8, 1)]
void dilate_copy() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(texindex);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}

	float4 index4 = texindex[id.xy];
	if (index4.r == 0.0) {
		return;
	}

	target[id.xy] = source[id.xy];
}
