package arm;

import androidx.annotation.Keep;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

class AndroidHttpRequest {

	@Keep
	public static int status = 0;

	@Keep
	public static byte[] androidHttpRequest(String url_base, String url_path, String data, int method, String headers) throws Exception {
		status = 0;
		try {
			// https://developer.android.com/reference/java/net/HttpURLConnection.html
			URL               url           = new URL("https://" + url_base + "/" + url_path);
			HttpURLConnection urlConnection = (HttpURLConnection)url.openConnection();
			urlConnection.setRequestMethod(method == 1 ? "POST" : "GET");
			if (headers != null) {
				for (String line : headers.split("\r\n")) {
					int colon = line.indexOf(':');
					if (colon > 0) {
						urlConnection.setRequestProperty(line.substring(0, colon), line.substring(colon + 1).trim());
					}
				}
			}
			if (method == 1) {
				byte[] body = data != null ? data.getBytes("UTF-8") : new byte[0];
				urlConnection.setDoOutput(true);
				urlConnection.setFixedLengthStreamingMode(body.length);
				OutputStream out = urlConnection.getOutputStream();
				out.write(body);
				out.close();
			}
			int         code   = urlConnection.getResponseCode();
			InputStream stream = code >= 400 ? urlConnection.getErrorStream() : urlConnection.getInputStream();

			ByteArrayOutputStream buffer = new ByteArrayOutputStream();
			if (stream != null) {
				InputStream in = new BufferedInputStream(stream);
				int         i;
				byte[]      bytes = new byte[4096];
				while ((i = in.read(bytes, 0, bytes.length)) != -1) {
					buffer.write(bytes, 0, i);
				}
			}
			buffer.write(0);
			buffer.flush();
			byte[] result = buffer.toByteArray();

			urlConnection.disconnect();
			status = code;
			return result;
		} catch (Exception e) {
			return null;
		}
	}
}
