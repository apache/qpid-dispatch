/**
 * @module QDR
 */
var QDR = (function (QDR) {

    QDR.SchemaController = function(QDRService) {

        $scope.schema = QDRService.schema;

    };

    return QDR;
}(QDR || {}));
