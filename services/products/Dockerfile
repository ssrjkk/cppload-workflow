# @author ssrjkk | volley
FROM python:3.12-slim
WORKDIR /app
COPY app.py .
RUN groupadd -r app && useradd -r -g app -d /app -s /sbin/nologin app && \
    chown -R app:app /app
USER app
EXPOSE 8080
CMD ["python", "app.py"]
