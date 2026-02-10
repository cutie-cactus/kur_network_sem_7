FROM gcc:latest

WORKDIR /app
COPY . .

RUN apt-get update && apt-get install -y make
RUN make

EXPOSE 8080

CMD ["./server", "8080"]
