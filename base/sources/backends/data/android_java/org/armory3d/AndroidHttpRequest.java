package arm;

import androidx.annotation.Keep;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

class AndroidHttpRequest {

	@Keep
	public static byte[] androidHttpRequest(String url_base, String url_path) throws Exception {
		try {
			// https://developer.android.com/reference/java/net/HttpURLConnection.html
			URL               url           = new URL("https://" + url_base + "/" + url_path);
			HttpURLConnection urlConnection = (HttpURLConnection)url.openConnection();
			InputStream       in            = new BufferedInputStream(urlConnection.getInputStream());

			ByteArrayOutputStream buffer = new ByteArrayOutputStream();
			int                   i;
			byte[]                data = new byte[4];
			while ((i = in.read(data, 0, data.length)) != -1) {
				buffer.write(data, 0, i);
			}
			buffer.write(0);
			buffer.flush();
			byte[] result = buffer.toByteArray();

			urlConnection.disconnect();
			return result;
		} catch (Exception e) {
			return null;
		}
	}
}
