import logging
import socket
import threading


class Device:
    def __init__(self, device_id):
        self.device_id = device_id
        self.producer_socket = None  # int  * soc
        self.consumer_socket = None  # int  * soc
        self.temp_data = []


class PacketRouter:
    def __init__(self):
        self.devices = {}
        self.producer_socket = socket.socket()
        self.consumer_socket = socket.socket()

    def start_server(self):
        self.producer_socket.bind(('0.0.0.0', 10000))
        self.producer_socket.listen()
        self.consumer_socket.bind(('0.0.0.0', 10001))
        self.consumer_socket.listen()

        producer_thread = threading.Thread(target=self.handle_producers, daemon=True)
        consumer_thread = threading.Thread(target=self.handle_consumers, daemon=True)

        producer_thread.start()
        consumer_thread.start()
        producer_thread.join()
        consumer_thread.join()

    def stop_server(self):
        self.producer_socket.close()
        self.consumer_socket.close()

    def handle_producers(self):
        while True:
            try:
                print("waiting producer")
                client, address = self.producer_socket.accept()
                device_id = 100
                print("Connected producer")
                temp_data1 = client.recv(1024)
                print(f"handle_producers  {list(temp_data1)}")
                temp_data2 = client.recv(1024)
                print(f"handle_producers  {list(temp_data2)}")
                if device_id in self.devices:
                    device = self.devices[device_id]
                    device.producer_socket = client
                    if device.consumer_socket:
                        self.route_packets(device)
                else:
                    self.devices[device_id] = Device(device_id)
                    self.devices[device_id].producer_socket = client

                self.devices[device_id].temp_data.append(temp_data1)
                self.devices[device_id].temp_data.append(temp_data2)

            except Exception as e:
                logging.error(f"handle_producers {e}")
                self.producer_socket.close()
                break

    def handle_consumers(self):
        while True:
            try:
                print("waiting consumer")
                client, address = self.consumer_socket.accept()
                device_id = 100
                print("Connected consumer")

                if device_id in self.devices:
                    device = self.devices[device_id]
                    device.consumer_socket = client
                    if device.producer_socket:
                        self.route_packets(device)
                else:
                    self.devices[device_id] = Device(device_id)
                    self.devices[device_id].consumer_socket = client

                temp_data1 = client.recv(1024)
                print(f"handle_consumers  {list(temp_data1)}")
                client.send(self.devices[device_id].temp_data[0])
                temp_data2 = client.recv(1024)
                print(f"handle_consumers  {list(temp_data2)}")
                client.send(self.devices[device_id].temp_data[1])
                print("Sent both data")

            except Exception as e:
                logging.error(f"handle_consumers {e}")
                self.consumer_socket.close()
                break

    def route_packets(self, device):
        producer_to_consumer_thread = threading.Thread(target=self.route_packets_producer_to_consumer, args=(device,),
                                                       daemon=True)
        consumer_to_producer_thread = threading.Thread(target=self.route_packets_consumer_to_producer, args=(device,),
                                                       daemon=True)

        producer_to_consumer_thread.start()
        consumer_to_producer_thread.start()

    def route_packets_producer_to_consumer(self, device):
        while True:
            try:
                data = device.producer_socket.recv(1024)
                if len(data) == 0:
                    device.producer_socket.close()
                    break
                print(f"route_packets_producer_to_consumer  {list(data)}")
                device.consumer_socket.send(data)
            except Exception as e:
                logging.error(f"route_packets_producer_to_consumer {e}")
                device.producer_socket.close()
                break

    def route_packets_consumer_to_producer(self, device):
        while True:
            try:
                data = device.consumer_socket.recv(1024)
                if len(data) == 0:
                    device.consumer_socket.close()
                    break
                print(f"route_packets_consumer_to_producer  {list(data)}")
                device.producer_socket.send(data)
            except Exception as e:
                logging.error(f"route_packets_consumer_to_producer {e}")
                device.consumer_socket.close()
                break


if __name__ == '__main__':
    router = PacketRouter()
    router.start_server()
