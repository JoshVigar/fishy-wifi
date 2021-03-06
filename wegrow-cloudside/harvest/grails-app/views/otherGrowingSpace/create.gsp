<!DOCTYPE html>
<html>
    <head>
        <link rel="stylesheet" type="text/css"
              href="grails-app/assets/stylesheets/application_conventional.less"/><meta name="layout" content="main" />
        <g:set var="entityName" value="${message(code: 'otherGrowingSpace.label', default: 'OtherGrowingSpace')}" />
        <title><g:message code="default.create.label" args="[entityName]" /></title>
    </head>
    <body>
        <div id="create-otherGrowingSpace" class="content scaffold-create" role="main">
            <h1><g:message code="default.create.label" args="[entityName]" /></h1>
            <g:if test="${flash.message}">
            <div class="message" role="status">${flash.message}</div>
            </g:if>
            <g:hasErrors bean="${this.otherGrowingSpace}">
            <ul class="errors" role="alert">
                <g:eachError bean="${this.otherGrowingSpace}" var="error">
                <li <g:if test="${error in org.springframework.validation.FieldError}">data-field-id="${error.field}"</g:if>><g:message error="${error}"/></li>
                </g:eachError>
            </ul>
            </g:hasErrors>
            <g:form action="save">
                <fieldset class="form">
                    <f:allWTransients bean="otherGrowingSpace"/>
                </fieldset>
                <fieldset class="buttons">
                    <g:submitButton name="create" class="btn btn-primary save" value="${message(code: 'default.button.create.label', default: 'Create')}" />
                </fieldset>
            </g:form>
            <div id="organic_footnote" >
                <g:message code="growingSpace.organicFootnote"></g:message>
            </div>
        </div>



    </body>
</html>
