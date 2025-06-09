# Techin515_Lab5

This repository includes the complete source code, models, training scripts, and documentation for an Edge-to-Cloud gesture recognition system using ESP32 and Flask. Expanding on Lab 4, the project improves performance and user experience by offloading low-confidence predictions from the edge device to a cloud-based model hosted on Microsoft Azure. This edge-first, cloud-assisted approach enhances gesture classification accuracy while maintaining a balance between latency, reliability, and resource efficiency.
  ```
Disscusions
1. Is server’s confidence always higher than wand’s confidence from your observations? What is your hypothetical reason for the observation?
Not always. In most cases, the server’s confidence is slightly higher than the wand’s, but sometimes it’s lower.
One possible reason is that the server model was trained on more diverse and larger datasets, especially if we merged datasets from multiple students. However, latency and noise in the transmitted data may occasionally reduce the server’s prediction confidence.
2. Sketch the data flow of this lab.

3. Analyze pros and cons of this edge-first, fallback-to-server approach:
Pros: It has lower latency for high-confidence predictions; reduces cloud cost and bandwidth use; still ensures accurate results when unsure (fallback to server); works even if server is temporarily unavailable (partial functionality).

Cons: It relies on network for cloud fallback—no internet = no backup; prediction results may vary between local and server model; potential data privacy concerns when sending raw sensor data to server; harder to maintain consistency if local and cloud models are not aligned.

4. Name a strategy to mitigate at least one limitation named in question 3.
To keep predictions consistent, we can regularly update the ESP32’s local model so it matches the server’s version. This helps both models stay in sync. Compressing the model also makes it run better on the device.

⸻

