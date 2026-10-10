class IronAudioProcessor extends AudioWorkletProcessor {
	constructor(options) {
		super();
		let o      = options.processorOptions;
		this.left  = new Float32Array(o.buffer, o.left, o.size);
		this.right = new Float32Array(o.buffer, o.right, o.size);
		this.read  = new Uint32Array(o.buffer, o.read, 1);
		this.write = new Uint32Array(o.buffer, o.write, 1);
		this.size  = o.size;
	}

	process(inputs, outputs) {
		let l     = outputs[0][0];
		let r     = outputs[0][1];
		let read  = Atomics.load(this.read, 0);
		let write = Atomics.load(this.write, 0);
		for (let i = 0; i < l.length && read != write; ++i) {
			l[i] = this.left[read];
			r[i] = this.right[read];
			read = (read + 1) % this.size;
		}
		Atomics.store(this.read, 0, read);
		return true;
	}
}

registerProcessor('iron-audio', IronAudioProcessor);
