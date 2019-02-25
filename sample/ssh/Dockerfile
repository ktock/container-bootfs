FROM rastasheep/ubuntu-sshd:latest

RUN apt update -y && apt install -y casync

CMD ["/usr/sbin/sshd", "-D"]
