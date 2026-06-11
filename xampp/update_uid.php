<?php
header('Content-Type: application/json');
$conn = new mysqli('localhost', 'root', '', 'rfid_db');

$body    = json_decode(file_get_contents('php://input'), true);
$user_id = (int)($body['user_id'] ?? 0);
$new_uid = strtoupper($conn->real_escape_string($body['new_uid'] ?? ''));

$conn->query("UPDATE users SET uid='$new_uid' WHERE id=$user_id");
echo json_encode(['success' => $conn->affected_rows > 0]);
$conn->close();